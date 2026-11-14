#![allow(clippy::unwrap_used, reason = "test helpers panic to fail")]
mod common;

use common::{connect_to, hello, read, sign_up, start_server_with_store, write};
use frontier_protocol::snapshot::{EntityKind, Snapshot};
use frontier_protocol::{ClientMessage, ServerMessage, WingmanHire, WingmanOrder};
use frontier_server::ledger::{Item, Npc, Party, Reason, Transfer};

#[tokio::test]
async fn hiring_is_gated_charged_and_visible_as_an_ally_in_snapshots() {
    let (url, hash, store) = start_server_with_store();
    let (session, mut send, mut recv) = connect_to(&url, hash).await;
    hello(&mut send, &mut recv).await;
    let (callsign, _) = sign_up(&mut send, &mut recv).await;
    read(&mut recv).await;
    let account = store.find(&callsign).unwrap().unwrap();
    write(
        &mut send,
        &ClientMessage::Launch {
            tier: 1,
            wingman: Some(WingmanHire { rig: None }),
        },
    )
    .await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::LaunchRefused { .. }
    ));
    assert_eq!(store.load(account).unwrap().credits, 100);
    let batch: Vec<Transfer> = [(Item::Credits, 1000), (Item::ORE, 40)]
        .into_iter()
        .map(|(item, amount)| Transfer {
            item,
            amount,
            from: Party::Npc(Npc::Grant),
            to: Party::Account(account),
            reason: Reason::StartingBalance,
            run: None,
        })
        .collect();
    store.transfer(&batch).unwrap();
    write(
        &mut send,
        &ClientMessage::Install {
            socket: "a".into(),
            facility: "crew_quarters".into(),
        },
    )
    .await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Account { .. }
    ));
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Contract { .. }
    ));
    write(
        &mut send,
        &ClientMessage::Launch {
            tier: 1,
            wingman: Some(WingmanHire { rig: None }),
        },
    )
    .await;
    let ServerMessage::Account { account: holdings } = read(&mut recv).await else {
        panic!("expected holdings")
    };
    assert_eq!(holdings.credits, 870);
    let ServerMessage::Joined {
        wingman: Some(info),
        ..
    } = read(&mut recv).await
    else {
        panic!("expected wingman")
    };
    let datagram = session.read_datagram().await.unwrap();
    let snapshot = Snapshot::decode(&datagram).unwrap();
    assert!(
        snapshot
            .entities
            .iter()
            .any(|e| e.id == info.ship_id && e.kind == EntityKind::Wingman)
    );
    write(
        &mut send,
        &ClientMessage::WingmanOrder {
            order: WingmanOrder::Hold { x: 20, z: 20 },
        },
    )
    .await;
    loop {
        if let ServerMessage::Wingman { info: Some(info) } = read(&mut recv).await
            && matches!(info.order, WingmanOrder::Hold { x: 20, z: 20 })
        {
            break;
        }
    }
    session.close(0_u32, b"test complete");
}
