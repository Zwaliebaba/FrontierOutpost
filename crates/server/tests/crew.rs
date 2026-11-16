//! Crews over the wire (plan M6): pilots form by invite code, the host launches
//! and the others follow into one instance, access is shared, a destroyed
//! member goes home while the crew flies on, and a disconnected one is
//! abandoned when the instance ends. Four pilots leave the station's hangar in
//! turn and dock through its mouth (plan 9.5).
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
use frontier_protocol::snapshot::{
    EntityKind, HEADING_UNITS_PER_TURN, POSITION_UNITS_PER_METRE, RunPhase, Snapshot,
    SnapshotEvent, VELOCITY_UNITS_PER_METRE_PER_SECOND,
};
use frontier_protocol::{
    ClientMessage, CrewInfo, Ending, Good, PocketKind, ServerMessage, SiteInfo, StationInfo,
};
use frontier_server::ledger::{Item, Npc, Party, Reason, Transfer};
use frontier_server::transport;
use frontier_sim::profile::ShipKind;
use frontier_sim::ship::ShipState;
use frontier_sim::wingman;
use frontier_sim::world::{Ship, ShipId};
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
        self.say(&ClientMessage::Launch {
            tier: 1,
            wingman: None,
        })
        .await;
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
    wing.say(&ClientMessage::Launch {
        tier: 1,
        wingman: None,
    })
    .await;
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
    wing.say(&ClientMessage::Launch {
        tier: 1,
        wingman: None,
    })
    .await;
    let ServerMessage::Account { account } = wing.hear().await else {
        panic!("expected the account");
    };
    assert_eq!(account.credits, 60, "supplies paid once");
    assert!(matches!(wing.hear().await, ServerMessage::Joined { .. }));

    // Out of the hangar, off the lane and straight back in, well inside the minute
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
    wing.say(&ClientMessage::Launch {
        tier: 1,
        wingman: None,
    })
    .await;
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

    // The host flies out of the hangar and straight back in.
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

/// One pilot of a crew of four, flown by a small autopilot from what its
/// own snapshots show: out of the hangar, off the lane to its own place,
/// and, in turn, home through the hangar mouth.
struct Flight {
    pilot: Pilot,
    ship: u16,
    station: StationInfo,
    /// Where it waits, in the station's frame `(across, along)`.
    spot: (f32, f32),
    going_home: bool,
    /// The latest own state: position, heading and speed.
    at: Option<(f32, f32, f32, f32)>,
    phase: Option<RunPhase>,
    /// Where it was when its launch handed it over, along the station's axis.
    handed_over_at: Option<f32>,
}

impl Flight {
    /// A world position in the station's frame.
    fn local(&self, (x, z): (f32, f32)) -> (f32, f32) {
        let s = &self.station;
        let (dx, dz) = (x - s.centre_x, z - s.centre_z);
        (
            dx * -s.forward_z + dz * s.forward_x,
            dx * s.forward_x + dz * s.forward_z,
        )
    }

    /// A point in the station's frame in world coordinates.
    fn world(&self, (across, along): (f32, f32)) -> (f32, f32) {
        let s = &self.station;
        (
            s.centre_x - across * s.forward_z + along * s.forward_x,
            s.centre_z + across * s.forward_x + along * s.forward_z,
        )
    }

    /// The controls the autopilot holds this tick.
    fn controls(&self) -> Controls {
        let Some((x, z, heading, speed)) = self.at else {
            return IDLE;
        };
        if self.phase != Some(RunPhase::Approach) {
            // On the launch's or the draw-in's script: input does nothing.
            return IDLE;
        }
        let (across, along) = self.local((x, z));
        let lane_end = 14.0 + self.station.lane_length;
        let (goal, stop) = if !self.going_home {
            (self.world(self.spot), 1.5)
        } else if across.abs() < 2.0 && (14.0..=lane_end + 20.0).contains(&along) {
            // Lined up: straight in at the mouth, where the capture zone takes it.
            ((self.station.mouth_x, self.station.mouth_z), 0.0)
        } else {
            (self.world((0.0, lane_end + 16.0)), 1.0)
        };
        let ship = Ship::new(
            ShipId(self.ship),
            ShipKind::Pilot,
            ShipState {
                x,
                z,
                heading,
                speed,
            },
        );
        let input = wingman::steer(&ship, goal, stop, false);
        Controls {
            thrust: quantize_control(input.thrust),
            turn: quantize_control(input.turn),
            ..IDLE
        }
    }

    fn observe(&mut self, snapshot: &Snapshot) {
        let Some(me) = snapshot.entities.iter().find(|e| e.id == self.ship) else {
            self.phase = Some(snapshot.own.phase);
            return;
        };
        let x = f32::from(me.x) / POSITION_UNITS_PER_METRE;
        let z = f32::from(me.z) / POSITION_UNITS_PER_METRE;
        let heading = f32::from(me.heading) / HEADING_UNITS_PER_TURN * std::f32::consts::TAU;
        let speed = f32::from(me.vx).hypot(f32::from(me.vz)) / VELOCITY_UNITS_PER_METRE_PER_SECOND;
        if self.phase == Some(RunPhase::AtDock) && snapshot.own.phase == RunPhase::Approach {
            self.handed_over_at = Some(self.local((x, z)).1);
        }
        self.at = Some((x, z, heading, speed));
        self.phase = Some(snapshot.own.phase);
    }
}

/// Reads past crew updates and contracts to the next message of interest.
async fn next_but_crew(pilot: &mut Pilot) -> ServerMessage {
    loop {
        match pilot.hear().await {
            ServerMessage::Crew { .. } | ServerMessage::Contract { .. } => {}
            other => return other,
        }
    }
}

/// Plan 9.5's proof: four pilots launch, leave the hangar one after another
/// in roster order without touching, and each docks through the mouth.
#[tokio::test]
async fn four_pilots_launch_from_the_hangar_in_turn_and_dock_through_its_mouth() {
    let settings = transport::Settings {
        opening_threat: Some(0),
        ..test_settings()
    };
    let (url, sha256) = start_server_with(settings, transport::IDLE_TIMEOUT);
    let mut pilots = Vec::new();
    for _ in 0..4 {
        pilots.push(arrive(&url, sha256).await.0);
    }
    pilots[0].say(&ClientMessage::CreateCrew).await;
    let (formed, _) = pilots[0].crew().await;
    for joining in 1..4 {
        pilots[joining]
            .say(&ClientMessage::JoinCrew {
                code: formed.code.clone(),
            })
            .await;
        for pilot in &mut pilots[..=joining] {
            pilot.crew().await;
        }
    }

    // The host launches, and the others follow in roster order.
    let mut flights = Vec::new();
    for (index, mut pilot) in pilots.into_iter().enumerate() {
        pilot
            .say(&ClientMessage::Launch {
                tier: 1,
                wingman: None,
            })
            .await;
        assert!(matches!(
            next_but_crew(&mut pilot).await,
            ServerMessage::Account { .. }
        ));
        let ServerMessage::Joined {
            ship_id,
            site,
            obstacles,
            ..
        } = next_but_crew(&mut pilot).await
        else {
            panic!("expected Joined");
        };
        assert!(!site.station.obstacles.is_empty(), "the hull is solid");
        assert!(
            obstacles
                .iter()
                .all(|rock| !site.station.obstacles.contains(rock)),
            "the asteroids come apart from the hull"
        );
        let across = [-36.0, -22.0, 22.0, 36.0][index];
        flights.push(Flight {
            pilot,
            ship: ship_id,
            station: site.station,
            spot: (across, 70.0),
            going_home: false,
            at: None,
            phase: None,
            handed_over_at: None,
        });
    }

    let mut left = Vec::new();
    let mut docked = Vec::new();
    for _ in 0..3_000 {
        for flight in &mut flights {
            let input = InputDatagram {
                sequence: flight.pilot.sequence,
                controls: vec![flight.controls(); 4],
            };
            flight.pilot.sequence = flight.pilot.sequence.wrapping_add(1);
            let _ = flight
                .pilot
                .session
                .send_datagram(input.encode().unwrap().into());
        }
        let deadline = tokio::time::Instant::now() + Duration::from_millis(50);
        for flight in &mut flights {
            while let Ok(Ok(bytes)) =
                tokio::time::timeout_at(deadline, flight.pilot.session.read_datagram()).await
            {
                flight.observe(&Snapshot::decode(&bytes).unwrap());
            }
        }
        for (index, flight) in flights.iter().enumerate() {
            if flight.handed_over_at.is_some() && !left.contains(&index) {
                left.push(index);
            }
            let home = matches!(flight.phase, Some(RunPhase::Docking | RunPhase::Docked));
            if home && !docked.contains(&index) {
                docked.push(index);
            }
        }
        // Once all four are out, home one at a time: the next sets off
        // when the one before has been caught by the mouth.
        if left.len() == 4
            && let Some(next) = flights.iter().position(|flight| !flight.going_home)
            && (next == 0 || docked.contains(&(next - 1)))
        {
            flights[next].going_home = true;
        }
        if docked.len() == 4 {
            break;
        }
    }
    assert_eq!(left, [0, 1, 2, 3], "out in roster order");
    for flight in &flights {
        let along = flight.handed_over_at.unwrap();
        let lane_end = 14.0 + flight.station.lane_length;
        assert!(
            (lane_end - 0.5..lane_end + 2.0).contains(&along),
            "handed over at the lane's end, {along} m out"
        );
    }
    assert_eq!(docked, [0, 1, 2, 3], "home in the order sent");
    for flight in &mut flights {
        let summary = loop {
            if let ServerMessage::RunOver { summary } = flight.pilot.hear().await {
                break summary;
            }
        };
        assert_eq!(summary.ending, Ending::Docked);
    }
}
