//! Crews over the wire (plan M6): pilots form by invite code, the host launches
//! and the others follow into one instance, access is shared, a destroyed
//! member goes home while the crew flies on, and a disconnected one is
//! abandoned when the instance ends.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

mod common;

use std::time::Duration;

use common::{
    connect_to, hello, read, sign_up, start_server_with, start_server_with_store, test_settings,
    write,
};
use frontier_protocol::input::{Controls, InputDatagram, quantize_control};
use frontier_protocol::snapshot::{EntityKind, Snapshot, SnapshotEvent};
use frontier_protocol::{
    ClientMessage, CrewInfo, Ending, Good, PocketKind, ServerMessage, SiteInfo,
};
use frontier_server::ledger::{Item, Npc, Party, Reason, Transfer};
use frontier_server::transport;
use web_transport_quinn::{RecvStream, SendStream, Session};

struct Pilot {
    session: Session,
    send: SendStream,
    recv: RecvStream,
    callsign: String,
    token: String,
    sequence: u16,
}

const FORWARD: Controls = Controls {
    thrust: 127,
    turn: 0,
    fire: false,
    cruise: false,
};
const BRAKE: Controls = Controls {
    thrust: -127,
    ..FORWARD
};
const IDLE: Controls = Controls {
    thrust: 0,
    ..FORWARD
};

/// Connects and signs up a new pilot, and reads their first contract.
async fn arrive(url: &url::Url, sha256: [u8; 32]) -> (Pilot, SiteInfo) {
    let (session, mut send, mut recv) = connect_to(url, sha256).await;
    hello(&mut send, &mut recv).await;
    let (callsign, token) = sign_up(&mut send, &mut recv).await;
    let ServerMessage::Contract { site, .. } = read(&mut recv).await else {
        panic!("expected a contract");
    };
    let pilot = Pilot {
        session,
        send,
        recv,
        callsign,
        token,
        sequence: 0,
    };
    (pilot, site)
}

impl Pilot {
    async fn say(&mut self, message: &ClientMessage) {
        write(&mut self.send, message).await;
    }

    async fn hear(&mut self) -> ServerMessage {
        tokio::time::timeout(Duration::from_secs(5), read(&mut self.recv))
            .await
            .expect("a message within five seconds")
    }

    /// The crew update and the crew's contract that follow any crew change.
    async fn crew(&mut self) -> (CrewInfo, SiteInfo) {
        let ServerMessage::Crew { crew: Some(crew) } = self.hear().await else {
            panic!("expected the crew");
        };
        let ServerMessage::Contract { site, .. } = self.hear().await else {
            panic!("expected the crew's contract");
        };
        (crew, site)
    }

    /// Launches (or follows the host) and returns the ship.
    async fn launch(&mut self) -> u16 {
        self.say(&ClientMessage::Launch { tier: 1 }).await;
        assert!(matches!(self.hear().await, ServerMessage::Account { .. }));
        let ServerMessage::Joined { ship_id, .. } = self.hear().await else {
            panic!("expected Joined");
        };
        ship_id
    }

    async fn fly(&mut self, controls: Controls, ticks: u32) -> Vec<Snapshot> {
        let mut snapshots = Vec::new();
        for _ in 0..ticks {
            let input = InputDatagram {
                sequence: self.sequence,
                controls: vec![controls; 4],
            };
            self.sequence = self.sequence.wrapping_add(1);
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

/// The host forms a crew and `wing` joins it by code; both read the updates.
async fn crew_up(host: &mut Pilot, wing: &mut Pilot) -> CrewInfo {
    host.say(&ClientMessage::CreateCrew).await;
    let (formed, _) = host.crew().await;
    assert!(formed.hosting);
    wing.say(&ClientMessage::JoinCrew {
        code: formed.code.to_lowercase(),
    })
    .await;
    let (joined, _) = wing.crew().await;
    let (seen, _) = host.crew().await;
    assert_eq!(seen.members.len(), 2);
    joined
}

#[tokio::test]
async fn two_pilots_crew_by_code_and_the_wing_follows_the_host_into_one_instance() {
    let (url, sha256, _store) = start_server_with_store();
    let (mut host, host_site) = arrive(&url, sha256).await;
    let (mut wing, _) = arrive(&url, sha256).await;

    host.say(&ClientMessage::CreateCrew).await;
    let (formed, _) = host.crew().await;
    assert_eq!(formed.code.len(), 6);
    wing.say(&ClientMessage::JoinCrew {
        code: formed.code.clone(),
    })
    .await;
    let (crew, wing_site) = wing.crew().await;
    assert!(!crew.hosting);
    let shares: Vec<(String, u32, bool)> = crew
        .members
        .iter()
        .map(|m| (m.callsign.clone(), m.share, m.you))
        .collect();
    assert_eq!(
        shares,
        [
            (host.callsign.clone(), 50, false),
            (wing.callsign.clone(), 50, true)
        ]
    );
    assert_eq!(
        wing_site, host_site,
        "the wing is offered the host's contract"
    );
    host.crew().await;

    // Only the host launches; the wing follows.
    wing.say(&ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        wing.hear().await,
        ServerMessage::LaunchRefused { .. }
    ));
    let host_ship = host.launch().await;
    let (crew, _) = wing.crew().await;
    assert_eq!(crew.launch.map(|launch| launch.tier), Some(1));
    let wing_ship = wing.launch().await;
    assert_ne!(wing_ship, host_ship);

    let snapshots = wing.fly(IDLE, 10).await;
    let ships: Vec<u16> = snapshots
        .last()
        .unwrap()
        .entities
        .iter()
        .filter(|e| e.kind == EntityKind::PlayerShip)
        .map(|e| e.id)
        .collect();
    assert!(
        ships.contains(&host_ship) && ships.contains(&wing_ship),
        "{ships:?}"
    );
}

#[tokio::test]
async fn a_crew_shares_what_its_outposts_open_and_the_host_sets_the_split() {
    let (url, sha256, store) = start_server_with_store();
    let (mut host, host_site) = arrive(&url, sha256).await;
    let (mut wing, _) = arrive(&url, sha256).await;
    let derelict_locked = |site: &SiteInfo| {
        site.pockets
            .iter()
            .find(|p| p.kind == PocketKind::Derelict)
            .unwrap()
            .locked
    };
    assert!(derelict_locked(&host_site));
    // The wing's outpost has a sensor array.
    let id = store.find(&wing.callsign).unwrap().unwrap();
    let grant = |item, amount| Transfer {
        item,
        amount,
        from: Party::Npc(Npc::Grant),
        to: Party::Account(id),
        reason: Reason::StartingBalance,
        run: None,
    };
    store
        .transfer(&[grant(Item::Credits, 60), grant(Item::ORE, 10)])
        .unwrap();
    wing.say(&ClientMessage::Install {
        socket: "a".into(),
        facility: "sensor_array".into(),
    })
    .await;
    assert!(matches!(wing.hear().await, ServerMessage::Account { .. }));
    assert!(matches!(wing.hear().await, ServerMessage::Contract { .. }));

    host.say(&ClientMessage::CreateCrew).await;
    let (formed, _) = host.crew().await;
    wing.say(&ClientMessage::JoinCrew { code: formed.code })
        .await;
    wing.crew().await;
    let (_, site) = host.crew().await;
    assert!(
        !derelict_locked(&site),
        "the wing's sensor array opens it for the crew"
    );

    wing.say(&ClientMessage::SetSplit {
        shares: vec![10, 90],
    })
    .await;
    assert!(matches!(
        wing.hear().await,
        ServerMessage::CrewRefused { .. }
    ));
    host.say(&ClientMessage::SetSplit {
        shares: vec![70, 30],
    })
    .await;
    let (crew, _) = host.crew().await;
    assert_eq!(
        crew.members.iter().map(|m| m.share).collect::<Vec<_>>(),
        [70, 30]
    );
    let (crew, _) = wing.crew().await;
    assert_eq!(crew.members[1].share, 30, "every pilot sees the split");
}

#[tokio::test]
async fn a_member_back_from_the_hosts_launch_is_not_offered_or_charged_for_it_again() {
    let settings = transport::Settings {
        opening_threat: Some(0),
        ..test_settings()
    };
    let (url, sha256) = start_server_with(settings, transport::IDLE_TIMEOUT);
    let (mut host, _) = arrive(&url, sha256).await;
    let (mut wing, _) = arrive(&url, sha256).await;
    crew_up(&mut host, &mut wing).await;
    host.launch().await;
    let (crew, _) = wing.crew().await;
    assert!(crew.launch.is_some(), "the host's launch is offered");
    wing.say(&ClientMessage::Launch { tier: 1 }).await;
    let ServerMessage::Account { account } = wing.hear().await else {
        panic!("expected the account");
    };
    assert_eq!(account.credits, 60, "supplies paid once");
    assert!(matches!(wing.hear().await, ServerMessage::Joined { .. }));

    // Out past the undock radius and straight back, well inside the minute
    // the host's launch stays open.
    wing.fly(FORWARD, 100).await;
    wing.fly(BRAKE, 60).await;
    let about = Controls {
        turn: quantize_control(1.0),
        ..IDLE
    };
    wing.fly(about, 26).await;
    wing.fly(FORWARD, 200).await;
    assert!(matches!(wing.hear().await, ServerMessage::Account { .. }));
    let ServerMessage::RunOver { summary } = wing.hear().await else {
        panic!("expected RunOver");
    };
    assert_eq!(summary.ending, Ending::Docked);
    assert!(!summary.run.is_empty());
    assert!(
        !summary.settled,
        "the crew is still flying; zero payout is pending"
    );
    loop {
        match wing.hear().await {
            ServerMessage::Crew { crew } => assert!(
                crew.is_none_or(|crew| crew.launch.is_none()),
                "a launch they have flown is not offered again"
            ),
            ServerMessage::Contract { .. } => break,
            _ => {}
        }
    }

    // Pressing "Follow the host" all the same once took 40 cr, then 20, for
    // a seat the instance refused.
    wing.say(&ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        wing.hear().await,
        ServerMessage::LaunchRefused { .. }
    ));
    wing.say(&ClientMessage::Buy {
        good: Good::Ore,
        amount: 1,
    })
    .await;
    let ServerMessage::Account { account } = wing.hear().await else {
        panic!("expected the account");
    };
    assert_eq!(account.credits, 60 - 16, "nothing taken by the refusal");
}

#[tokio::test]
async fn a_disconnected_member_is_abandoned_when_the_rest_of_the_crew_docks() {
    let settings = transport::Settings {
        opening_threat: Some(0),
        ..test_settings()
    };
    let (url, sha256) = start_server_with(settings, transport::IDLE_TIMEOUT);
    let (mut host, _) = arrive(&url, sha256).await;
    let (mut wing, _) = arrive(&url, sha256).await;
    crew_up(&mut host, &mut wing).await;
    host.launch().await;
    wing.crew().await;
    wing.launch().await;
    let token = wing.token.clone();
    wing.session.close(0, b"dropped");

    // The host flies out past the undock radius and straight back.
    host.fly(FORWARD, 100).await;
    host.fly(BRAKE, 60).await;
    let about = Controls {
        turn: quantize_control(1.0),
        ..IDLE
    };
    host.fly(about, 26).await;
    host.fly(FORWARD, 200).await;
    assert!(matches!(host.hear().await, ServerMessage::Account { .. }));
    let ServerMessage::RunOver { summary } = host.hear().await else {
        panic!("expected RunOver");
    };
    assert_eq!(summary.ending, Ending::Docked);

    // The wing comes back to no run: it was abandoned when the instance ended.
    let (_again, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    write(&mut send, &ClientMessage::Resume { token }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::SignedIn { .. }
    ));
    assert!(matches!(read(&mut recv).await, ServerMessage::Crew { .. }));
    assert!(
        matches!(read(&mut recv).await, ServerMessage::Contract { .. }),
        "a new contract, not the old run"
    );
}

#[tokio::test]
async fn a_destroyed_member_goes_home_while_the_crew_flies_on() {
    let settings = transport::Settings {
        opening_threat: Some(10),
        ..test_settings()
    };
    let (url, sha256) = start_server_with(settings, transport::IDLE_TIMEOUT);
    let (mut host, _) = arrive(&url, sha256).await;
    let (mut wing, _) = arrive(&url, sha256).await;
    crew_up(&mut host, &mut wing).await;
    let host_ship = host.launch().await;
    wing.crew().await;
    wing.launch().await;

    // The host flies into the ore field and waits among the drones.
    host.fly(FORWARD, 320).await;
    let mut destroyed = false;
    for _ in 0..120 {
        let snapshots = host.fly(BRAKE, 20).await;
        destroyed |= snapshots.iter().any(|s| {
            s.events
                .iter()
                .any(|e| matches!(e, SnapshotEvent::Destroyed { ship, .. } if *ship == host_ship))
        });
        wing.fly(IDLE, 1).await;
        if destroyed {
            break;
        }
    }
    assert!(destroyed, "ten NPCs destroy a pilot who stays among them");
    assert!(matches!(host.hear().await, ServerMessage::Account { .. }));
    let ServerMessage::RunOver { summary } = host.hear().await else {
        panic!("expected RunOver");
    };
    assert_eq!(summary.ending, Ending::Destroyed);
    assert!(matches!(host.hear().await, ServerMessage::Contract { .. }));

    // The wing's run goes on, and the host manages their outpost meanwhile.
    let snapshots = wing.fly(IDLE, 10).await;
    assert!(!snapshots.is_empty(), "the wing still flies");
    host.say(&ClientMessage::Sell {
        good: frontier_protocol::Good::Ore,
        amount: 1,
    })
    .await;
    assert!(matches!(
        host.hear().await,
        ServerMessage::HomeRefused { .. }
    ));
}
