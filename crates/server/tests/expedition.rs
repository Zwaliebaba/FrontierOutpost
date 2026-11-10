//! Pilots join the expedition, steer with input datagrams and see every ship
//! in the snapshots the server sends back.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

mod common;

use std::time::Duration;

use common::{connect_to, hello, read, start_server, start_server_with_idle_timeout};
use frontier_protocol::ServerMessage;
use frontier_protocol::input::{Controls, InputDatagram, quantize_control};
use frontier_protocol::snapshot::{EntityKind, EntityState, Snapshot};
use web_transport_quinn::Session;

/// Joins a pilot and returns the session and the ship it flies.
async fn join(url: &url::Url, sha256: [u8; 32]) -> (Session, u16) {
    let (session, ship, send) = join_with_control(url, sha256).await;
    // Keep the control stream open for the life of the session.
    std::mem::forget(send);
    (session, ship)
}

/// Joins a pilot and also returns the control stream, to send messages on.
async fn join_with_control(
    url: &url::Url,
    sha256: [u8; 32],
) -> (Session, u16, web_transport_quinn::SendStream) {
    let (session, mut send, mut recv) = connect_to(url, sha256).await;
    assert!(matches!(
        hello(&mut send, &mut recv).await,
        ServerMessage::Welcome { .. }
    ));
    let ServerMessage::Joined {
        ship_id,
        tick_rate,
        obstacles,
        ..
    } = read(&mut recv).await
    else {
        panic!("expected Joined");
    };
    assert_eq!(tick_rate, frontier_sim::TICKS_PER_SECOND);
    assert!(
        !obstacles.is_empty(),
        "the asteroid field is sent on joining"
    );
    std::mem::forget(recv);
    (session, ship_id, send)
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
        fire: false,
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

/// A UDP forwarder that can be cut, so a client vanishes without closing its
/// session: what a closed browser tab can look like to the server.
async fn cuttable_link(
    server: std::net::SocketAddr,
) -> (
    std::net::SocketAddr,
    std::sync::Arc<std::sync::atomic::AtomicBool>,
) {
    use std::sync::Arc;
    use std::sync::atomic::{AtomicBool, Ordering};
    use tokio::net::UdpSocket;
    let front = Arc::new(UdpSocket::bind("127.0.0.1:0").await.unwrap());
    let back = Arc::new(UdpSocket::bind("127.0.0.1:0").await.unwrap());
    back.connect(server).await.unwrap();
    let cut = Arc::new(AtomicBool::new(false));
    let addr = front.local_addr().unwrap();
    let client = Arc::new(tokio::sync::Mutex::new(None));
    {
        let (front, back, cut, client) = (front.clone(), back.clone(), cut.clone(), client.clone());
        tokio::spawn(async move {
            let mut buffer = vec![0; 65_536];
            while let Ok((length, from)) = front.recv_from(&mut buffer).await {
                *client.lock().await = Some(from);
                if !cut.load(Ordering::Relaxed) {
                    let _ = back.send(&buffer[..length]).await;
                }
            }
        });
    }
    {
        let cut = cut.clone();
        tokio::spawn(async move {
            let mut buffer = vec![0; 65_536];
            while let Ok(length) = back.recv(&mut buffer).await {
                let to = *client.lock().await;
                if let (false, Some(to)) = (cut.load(Ordering::Relaxed), to) {
                    let _ = front.send_to(&buffer[..length], to).await;
                }
            }
        });
    }
    (addr, cut)
}

#[tokio::test]
async fn a_pilot_who_vanishes_without_closing_is_dropped_after_the_idle_timeout() {
    use std::sync::atomic::Ordering;
    let idle = Duration::from_millis(600);
    let (url, sha256) = start_server_with_idle_timeout(idle);
    let (watcher, watcher_ship) = join(&url, sha256).await;

    let server = format!("127.0.0.1:{}", url.port().unwrap())
        .parse()
        .unwrap();
    let (link, cut) = cuttable_link(server).await;
    let via_link: url::Url = format!("https://{link}/").parse().unwrap();
    let (_ghost, ghost_ship) = join(&via_link, sha256).await;
    let seen = last_snapshot(&watcher, Duration::from_millis(100)).await;
    assert!(pilots(&seen).contains(&ghost_ship));

    cut.store(true, Ordering::Relaxed);
    tokio::time::sleep(idle * 3).await;
    let seen = last_snapshot(&watcher, Duration::from_millis(100)).await;
    assert_eq!(
        pilots(&seen),
        [watcher_ship],
        "the vanished pilot's ship is gone"
    );
}

#[tokio::test]
async fn a_pilot_who_locks_a_drone_and_holds_the_trigger_fires_at_it() {
    use frontier_protocol::ClientMessage;
    use frontier_protocol::snapshot::SnapshotEvent;
    let (url, sha256) = start_server();
    let (pilot, ship, mut control) = join_with_control(&url, sha256).await;

    // The skirmish's drones notice the pilot and close in.
    let start = last_snapshot(&pilot, Duration::from_millis(100)).await;
    let drone = start
        .entities
        .iter()
        .find(|e| e.kind == EntityKind::Drone)
        .unwrap()
        .id;
    common::write(
        &mut control,
        &ClientMessage::Lock {
            target: Some(drone),
        },
    )
    .await;

    let trigger = Controls {
        thrust: 0,
        turn: 0,
        fire: true,
    };
    let mut fired_at_drone = false;
    for sequence in 0..160_u16 {
        let input = InputDatagram {
            sequence,
            controls: vec![trigger; usize::from(sequence.min(3)) + 1],
        };
        pilot.send_datagram(input.encode().unwrap().into()).unwrap();
        let deadline = tokio::time::Instant::now() + Duration::from_millis(50);
        while let Ok(Ok(bytes)) = tokio::time::timeout_at(deadline, pilot.read_datagram()).await {
            let snapshot = Snapshot::decode(&bytes).unwrap();
            fired_at_drone |= snapshot.events.iter().any(|event| {
                matches!(event, SnapshotEvent::Fired { shooter, target, .. }
                    if *shooter == ship && *target == drone)
            });
        }
        if fired_at_drone {
            break;
        }
    }
    assert!(fired_at_drone, "no shot at the locked drone within 8 s");
}
