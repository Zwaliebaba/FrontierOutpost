//! Plan work item 7.1: one session played over the wire records what every
//! Stage 1 gate measures (design section 9).
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

mod common;

use std::time::Duration;

use common::{connect_to, hello, read, sign_up, start_server_with_store, write};
use frontier_protocol::input::{Controls, InputDatagram, quantize_control};
use frontier_protocol::{ClientMessage, ServerMessage};
use frontier_server::events::GateEvent;
use frontier_server::ledger::{Item, Npc, Party, Reason, Transfer};
use frontier_server::report::{Players, Verdict};

const FORWARD: Controls = Controls {
    thrust: 127,
    turn: 0,
    fire: false,
    cruise: false,
};

#[tokio::test]
async fn a_session_records_sessions_launches_choices_reconfigurations_and_endings() {
    let (url, sha256, store) = start_server_with_store();
    let (session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    let (callsign, _) = sign_up(&mut send, &mut recv).await;
    read(&mut recv).await;
    let id = store.find(&callsign).unwrap().unwrap();

    // Build a refinery, then move it: one purchase, one reconfiguration.
    let grant = |item, amount| Transfer {
        item,
        amount,
        from: Party::Npc(Npc::Grant),
        to: Party::Account(id),
        reason: Reason::StartingBalance,
        run: None,
    };
    store
        .transfer(&[grant(Item::Credits, 200), grant(Item::ORE, 10)])
        .unwrap();
    for message in [
        ClientMessage::Install {
            socket: "a".into(),
            facility: "refinery".into(),
        },
        ClientMessage::Power {
            socket: "a".into(),
            run: 0,
        },
        ClientMessage::Power {
            socket: "a".into(),
            run: 1,
        },
    ] {
        write(&mut send, &message).await;
        assert!(matches!(
            read(&mut recv).await,
            ServerMessage::Account { .. }
        ));
        assert!(matches!(
            read(&mut recv).await,
            ServerMessage::Contract { .. }
        ));
    }

    // Launch, fly out past the undock radius, turn about and dock.
    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Account { .. }
    ));
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Joined { .. }
    ));
    let mut sequence = 0_u16;
    let mut fly = |controls: Controls, ticks: u32| {
        let datagrams: Vec<Vec<u8>> = (0..ticks)
            .map(|_| {
                sequence = sequence.wrapping_add(1);
                InputDatagram {
                    sequence,
                    controls: vec![controls; 4],
                }
                .encode()
                .unwrap()
            })
            .collect();
        datagrams
    };
    let about = Controls {
        thrust: 0,
        turn: quantize_control(1.0),
        ..FORWARD
    };
    let brake = Controls {
        thrust: -127,
        ..FORWARD
    };
    for (controls, ticks) in [(FORWARD, 100), (brake, 60), (about, 26), (FORWARD, 200)] {
        for datagram in fly(controls, ticks) {
            let _ = session.send_datagram(datagram.into());
            tokio::time::sleep(Duration::from_millis(50)).await;
        }
    }
    assert!(matches!(
        tokio::time::timeout(Duration::from_secs(5), read(&mut recv))
            .await
            .unwrap(),
        ServerMessage::Account { .. }
    ));
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::RunOver { .. }
    ));
    session.close(0, b"done");
    tokio::time::sleep(Duration::from_millis(300)).await;

    let events: Vec<GateEvent> = store
        .events()
        .unwrap()
        .into_iter()
        .filter(|recorded| recorded.account == id.0)
        .map(|recorded| recorded.event)
        .collect();
    let kinds: Vec<&str> = events.iter().map(GateEvent::kind).collect();
    for kind in [
        "session_started",
        "outpost_changed",
        "launched",
        "run_ended",
        "session_ended",
    ] {
        assert!(kinds.contains(&kind), "{kind} missing from {kinds:?}");
    }
    let actions: Vec<&str> = events
        .iter()
        .filter_map(|event| match event {
            GateEvent::OutpostChanged { action, .. } => Some(action.as_str()),
            _ => None,
        })
        .collect();
    assert_eq!(actions, ["bought", "powered", "powered"]);
    let launched = events
        .iter()
        .find_map(|event| match event {
            GateEvent::Launched {
                layout,
                unlocks,
                fitted,
                stake,
                ..
            } => Some((layout.len(), unlocks.clone(), fitted.clone(), *stake)),
            _ => None,
        })
        .unwrap();
    assert_eq!(
        launched,
        (
            1,
            vec!["refinery:1".to_owned()],
            vec!["extraction_rig".to_owned()],
            60
        )
    );
    let ending = events.iter().find_map(|event| match event {
        GateEvent::RunEnded { ending, .. } => Some(ending.as_str()),
        _ => None,
    });
    assert_eq!(ending, Some("docked"));

    // And the report computes every gate from them. The pilot reconfigured
    // early, but as the owner's play that leaves gate 1 unproven (plan 7.3).
    let report =
        frontier_server::report::compute(&store.events().unwrap(), &[], Players::Owner, Vec::new());
    assert_eq!((report.pilots, report.runs, report.sessions), (1, 1, 1));
    let reconfigured = report.gates[0]
        .measures
        .iter()
        .find(|m| m.name.starts_with("Pilots who reconfigure"))
        .unwrap();
    assert_eq!(reconfigured.met(), Some(true));
    assert_eq!(
        reconfigured.verdict(Players::Owner),
        Some(Verdict::Unproven)
    );
}
