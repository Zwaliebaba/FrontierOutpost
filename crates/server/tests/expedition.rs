//! A pilot takes the offered contract, launches into their own instance, flies
//! it with input datagrams, and comes home: docked or destroyed.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

mod common;

use std::time::Duration;

use common::{
    connect_to, hello, read, start_server, start_server_with, start_server_with_idle_timeout,
    test_settings, write,
};
use frontier_protocol::input::{Controls, InputDatagram, quantize_control};
use frontier_protocol::snapshot::{EntityKind, RunPhase, Snapshot, SnapshotEvent};
use frontier_protocol::{ClientMessage, Ending, ServerMessage};
use frontier_server::transport;
use web_transport_quinn::{RecvStream, SendStream, Session};

struct Pilot {
    session: Session,
    send: SendStream,
    recv: RecvStream,
    ship: u16,
    sequence: u16,
}

/// Connects, reads the contract and launches at tier I.
async fn launch(url: &url::Url, sha256: [u8; 32]) -> Pilot {
    let (session, mut send, mut recv) = connect_to(url, sha256).await;
    assert!(matches!(
        hello(&mut send, &mut recv).await,
        ServerMessage::Welcome { .. }
    ));
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Contract { .. }
    ));
    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    let ServerMessage::Joined { ship_id, .. } = read(&mut recv).await else {
        panic!("expected Joined");
    };
    Pilot {
        session,
        send,
        recv,
        ship: ship_id,
        sequence: 0,
    }
}

const FORWARD: Controls = Controls {
    thrust: 127,
    turn: 0,
    fire: false,
};
const BRAKE: Controls = Controls {
    thrust: -127,
    turn: 0,
    fire: false,
};
const IDLE: Controls = Controls {
    thrust: 0,
    turn: 0,
    fire: false,
};

impl Pilot {
    /// Sends `controls` once a tick for `ticks` ticks and returns every
    /// snapshot that arrived meanwhile.
    async fn fly(&mut self, controls: Controls, ticks: u32) -> Vec<Snapshot> {
        let mut snapshots = Vec::new();
        for _ in 0..ticks {
            let input = InputDatagram {
                sequence: self.sequence,
                controls: vec![controls; 4],
            };
            self.sequence = self.sequence.wrapping_add(1);
            // The session may close as the run ends; the test checks what it hears.
            let _ = self.session.send_datagram(input.encode().unwrap().into());
            let deadline = tokio::time::Instant::now() + Duration::from_millis(50);
            while let Ok(Ok(bytes)) =
                tokio::time::timeout_at(deadline, self.session.read_datagram()).await
            {
                snapshots.push(Snapshot::decode(&bytes).unwrap());
            }
        }
        snapshots
    }
}

#[tokio::test]
async fn the_contract_shows_the_site_and_only_opened_tiers_launch() {
    let (url, sha256) = start_server();
    let (_session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    let ServerMessage::Contract { site, tiers, .. } = read(&mut recv).await else {
        panic!("expected a contract");
    };
    assert_eq!(site.pockets.len(), 4);
    assert_eq!(site.pockets.iter().filter(|p| p.locked).count(), 3);
    assert_eq!(tiers.iter().filter(|t| t.open).count(), 1);

    write(&mut send, &ClientMessage::Launch { tier: 2 }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::LaunchRefused { .. }
    ));
    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    let ServerMessage::Joined {
        deposits,
        obstacles,
        tier,
        ..
    } = read(&mut recv).await
    else {
        panic!("expected Joined");
    };
    assert_eq!(tier, 1);
    assert_eq!(deposits.len(), 5, "the ore field's deposits");
    assert!(!obstacles.is_empty());
}

#[tokio::test]
async fn a_pilot_flies_with_inputs_and_leaves_the_dock() {
    let (url, sha256) = start_server();
    let mut pilot = launch(&url, sha256).await;
    let snapshots = pilot.fly(FORWARD, 100).await;
    let last = snapshots.last().unwrap();
    assert_eq!(last.own.phase, RunPhase::Approach, "five seconds out");
    assert!(last.input_ack >= 90);
    let me = last.entities.iter().find(|e| e.id == pilot.ship).unwrap();
    assert!(me.vx > 0, "flying toward the site");
}

#[tokio::test]
async fn each_launch_is_its_own_instance() {
    let (url, sha256) = start_server();
    let mut first = launch(&url, sha256).await;
    let mut second = launch(&url, sha256).await;
    for pilot in [&mut first, &mut second] {
        let snapshot = pilot.fly(IDLE, 5).await.pop().unwrap();
        let pilots: Vec<u16> = snapshot
            .entities
            .iter()
            .filter(|e| e.kind == EntityKind::PlayerShip)
            .map(|e| e.id)
            .collect();
        assert_eq!(pilots, [pilot.ship], "a solo run sees only its own pilot");
    }
}

#[tokio::test]
async fn flying_out_and_back_docks_and_a_new_contract_follows() {
    let (url, sha256) = start_server();
    let mut pilot = launch(&url, sha256).await;
    // Out past the undock radius, stop, turn about, and come straight back.
    pilot.fly(FORWARD, 100).await;
    pilot.fly(BRAKE, 60).await;
    let about = Controls {
        thrust: 0,
        turn: quantize_control(1.0),
        fire: false,
    };
    // 2.4 rad/s: half a turn in about 26 ticks.
    pilot.fly(about, 26).await;
    pilot.fly(FORWARD, 200).await;

    let ServerMessage::RunOver { summary } =
        tokio::time::timeout(Duration::from_secs(5), read(&mut pilot.recv))
            .await
            .unwrap()
    else {
        panic!("expected RunOver");
    };
    assert_eq!(summary.ending, Ending::Docked);
    assert_eq!(summary.payout, 0, "no cargo, no contract");
    assert_eq!(summary.launch_cost, 40);
    assert!(matches!(
        read(&mut pilot.recv).await,
        ServerMessage::Contract { .. }
    ));
    write(&mut pilot.send, &ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        read(&mut pilot.recv).await,
        ServerMessage::Joined { .. }
    ));
}

#[tokio::test]
async fn a_pilot_in_the_ore_field_can_lock_and_fire_at_a_drone_and_be_destroyed() {
    let settings = transport::Settings {
        opening_threat: Some(10),
        ..test_settings()
    };
    let (url, sha256) = start_server_with(settings, transport::IDLE_TIMEOUT);
    let mut pilot = launch(&url, sha256).await;
    // Into the ore field, about 165 m ahead.
    let mut snapshots = pilot.fly(FORWARD, 320).await;
    let trigger = Controls {
        fire: true,
        ..BRAKE
    };
    let mut fired = false;
    let mut destroyed = false;
    for _ in 0..60 {
        // As a player presses Tab: lock whichever hostile is nearest now.
        if let Some(last) = snapshots.last()
            && let Some(me) = last.entities.iter().find(|e| e.id == pilot.ship)
        {
            let distance = |e: &&frontier_protocol::snapshot::EntityState| {
                (f32::from(e.x) - f32::from(me.x)).hypot(f32::from(e.z) - f32::from(me.z))
            };
            let nearest = last
                .entities
                .iter()
                .filter(|e| e.kind != EntityKind::PlayerShip)
                .min_by(|a, b| distance(a).total_cmp(&distance(b)))
                .map(|e| e.id);
            write(&mut pilot.send, &ClientMessage::Lock { target: nearest }).await;
        }
        snapshots = pilot.fly(trigger, 20).await;
        fired |= snapshots.iter().any(|s| {
            s.events.iter().any(
                |e| matches!(e, SnapshotEvent::Fired { shooter, .. } if *shooter == pilot.ship),
            )
        });
        destroyed |= snapshots.iter().any(|s| {
            s.events
                .iter()
                .any(|e| matches!(e, SnapshotEvent::Destroyed { ship, .. } if *ship == pilot.ship))
        });
        if fired || destroyed {
            break;
        }
    }
    // Then hold fire among the rest until they finish the pilot.
    for _ in 0..90 {
        if destroyed {
            break;
        }
        let snapshots = pilot.fly(IDLE, 20).await;
        destroyed |= snapshots.iter().any(|s| {
            s.events
                .iter()
                .any(|e| matches!(e, SnapshotEvent::Destroyed { ship, .. } if *ship == pilot.ship))
        });
    }
    assert!(fired, "the pilot fired at the locked drone");
    assert!(destroyed, "ten NPCs destroy a pilot who stays among them");
    let ServerMessage::RunOver { summary } =
        tokio::time::timeout(Duration::from_secs(5), read(&mut pilot.recv))
            .await
            .unwrap()
    else {
        panic!("expected RunOver");
    };
    assert_eq!(summary.ending, Ending::Destroyed);
}

#[tokio::test]
async fn a_silent_session_is_dropped_after_the_idle_timeout() {
    let idle = Duration::from_millis(600);
    let (url, sha256) = start_server_with_idle_timeout(idle);
    let (session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    // At home nobody sends datagrams, so the connection goes quiet.
    tokio::time::timeout(idle * 4, session.closed())
        .await
        .expect("the session should time out");
}
