//! Pilots join the expedition, steer with input datagrams and see every ship
//! in the snapshots the server sends back.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

mod common;

use std::time::Duration;

use common::{connect_to, hello, read, start_server};
use frontier_protocol::ServerMessage;
use frontier_protocol::input::{Controls, InputDatagram, quantize_control};
use frontier_protocol::snapshot::{EntityKind, EntityState, Snapshot};
use web_transport_quinn::Session;

/// Joins a pilot and returns the session and the ship it flies.
async fn join(url: &url::Url, sha256: [u8; 32]) -> (Session, u16) {
    let (session, mut send, mut recv) = connect_to(url, sha256).await;
    assert!(matches!(
        hello(&mut send, &mut recv).await,
        ServerMessage::Welcome { .. }
    ));
    let ServerMessage::Joined {
        ship_id,
        tick_rate,
        obstacles,
    } = read(&mut recv).await
    else {
        panic!("expected Joined");
    };
    assert_eq!(tick_rate, frontier_sim::TICKS_PER_SECOND);
    assert!(
        !obstacles.is_empty(),
        "the asteroid field is sent on joining"
    );
    // Keep the control stream open for the life of the session.
    std::mem::forget((send, recv));
    (session, ship_id)
}

/// Reads snapshot datagrams for `duration` and returns the last one.
async fn last_snapshot(session: &Session, duration: Duration) -> Snapshot {
    let mut last = None;
    let deadline = tokio::time::Instant::now() + duration;
    while let Ok(Ok(bytes)) = tokio::time::timeout_at(deadline, session.read_datagram()).await {
        last = Some(Snapshot::decode(&bytes).unwrap());
    }
    last.expect("no snapshot arrived")
}

/// The ids of the player ships in a snapshot; the skirmish's NPCs are there too.
fn pilots(snapshot: &Snapshot) -> Vec<u16> {
    snapshot
        .entities
        .iter()
        .filter(|e| e.kind == EntityKind::PlayerShip)
        .map(|e| e.id)
        .collect()
}

fn find(snapshot: &Snapshot, id: u16) -> EntityState {
    *snapshot.entities.iter().find(|e| e.id == id).unwrap()
}

#[tokio::test]
async fn two_pilots_see_each_other_and_one_moves() {
    let (url, sha256) = start_server();
    let (pilot, pilot_ship) = join(&url, sha256).await;
    let (watcher, watcher_ship) = join(&url, sha256).await;
    assert_ne!(pilot_ship, watcher_ship);

    let start = find(
        &last_snapshot(&watcher, Duration::from_millis(200)).await,
        pilot_ship,
    );

    // Full thrust for one second, one input per tick, as the client sends them.
    let thrust = Controls {
        thrust: quantize_control(1.0),
        turn: 0,
    };
    for sequence in 0..20_u16 {
        let input = InputDatagram {
            sequence,
            controls: vec![thrust; usize::from(sequence.min(3)) + 1],
        };
        pilot.send_datagram(input.encode().unwrap().into()).unwrap();
        tokio::time::sleep(Duration::from_millis(50)).await;
    }

    let seen_by_watcher = last_snapshot(&watcher, Duration::from_millis(200)).await;
    let seen_by_pilot = last_snapshot(&pilot, Duration::from_millis(200)).await;
    for snapshot in [&seen_by_watcher, &seen_by_pilot] {
        let mut ids: Vec<u16> = pilots(snapshot);
        ids.sort_unstable();
        assert_eq!(
            ids,
            [pilot_ship.min(watcher_ship), pilot_ship.max(watcher_ship)]
        );
    }

    let moved = find(&seen_by_watcher, pilot_ship);
    let distance = f32::from(moved.x - start.x).hypot(f32::from(moved.z - start.z)) / 32.0;
    assert!(distance > 1.0, "the pilot's ship moved only {distance} m");
    let idle = find(&seen_by_pilot, watcher_ship);
    assert_eq!((idle.vx, idle.vz), (0, 0), "the watcher never thrust");
    assert_eq!(seen_by_pilot.input_ack, 19);
}

#[tokio::test]
async fn a_fifth_pilot_is_turned_away() {
    let (url, sha256) = start_server();
    let mut pilots = Vec::new();
    for _ in 0..4 {
        pilots.push(join(&url, sha256).await);
    }
    let (_session, mut send, mut recv) = connect_to(&url, sha256).await;
    assert!(matches!(
        hello(&mut send, &mut recv).await,
        ServerMessage::Welcome { .. }
    ));
    assert_eq!(read(&mut recv).await, ServerMessage::ExpeditionFull);
}

#[tokio::test]
async fn a_pilot_who_leaves_frees_their_place() {
    let (url, sha256) = start_server();
    let (stays, stays_ship) = join(&url, sha256).await;
    let (leaves, _) = join(&url, sha256).await;
    leaves.close(0, b"bye");
    tokio::time::sleep(Duration::from_millis(300)).await;
    let snapshot = last_snapshot(&stays, Duration::from_millis(200)).await;
    assert_eq!(pilots(&snapshot), [stays_ship]);
}
