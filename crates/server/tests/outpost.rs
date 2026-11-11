//! The home over the wire (plan M5): the catalog, building, trading, and a
//! purchase opening a locked pocket on the next run.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

mod common;

use common::{connect_to, hello, read, read_any, sign_up, start_server_with_store, write};
use frontier_protocol::{ClientMessage, Good, PocketKind, ServerMessage, SiteInfo};
use frontier_server::ledger::{Item, Npc, Party, Reason, Transfer};
use frontier_server::store::Store;

fn derelict_locked(site: &SiteInfo) -> bool {
    site.pockets
        .iter()
        .find(|pocket| pocket.kind == PocketKind::Derelict)
        .unwrap()
        .locked
}

/// Gives a pilot credits and ore, as if they had flown for them.
fn grant(store: &Store, callsign: &str, credits: u32, ore: u32) {
    let id = store.find(callsign).unwrap().unwrap();
    let grant = |item, amount| Transfer {
        item,
        amount,
        from: Party::Npc(Npc::Grant),
        to: Party::Account(id),
        reason: Reason::StartingBalance,
        run: None,
    };
    let batch: Vec<Transfer> = [(Item::Credits, credits), (Item::ORE, ore)]
        .into_iter()
        .filter(|&(_, amount)| amount > 0)
        .map(|(item, amount)| grant(item, amount))
        .collect();
    store.transfer(&batch).unwrap();
}

#[tokio::test]
async fn the_catalog_follows_signing_in() {
    let (url, sha256, _store) = start_server_with_store();
    let (_session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    sign_up(&mut send, &mut recv).await;
    // The catalog comes straight after SignedIn, before the contract.
    let next = read_any(&mut recv).await;
    let catalog = match next {
        ServerMessage::Catalog { catalog } => catalog,
        other => panic!("expected the catalog, got {other:?}"),
    };
    assert_eq!(catalog.facilities.len(), 3);
    assert_eq!(catalog.supplies, 40);
    assert!(matches!(
        read_any(&mut recv).await,
        ServerMessage::Contract { .. }
    ));
}

#[tokio::test]
async fn buying_a_facility_opens_its_pocket_on_the_next_run() {
    let (url, sha256, store) = start_server_with_store();
    let (_session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    let (callsign, _token) = sign_up(&mut send, &mut recv).await;
    let ServerMessage::Contract { site, .. } = read(&mut recv).await else {
        panic!("expected a contract");
    };
    assert!(derelict_locked(&site), "no sensor array yet");

    // A first run's worth: 260 cr and 10 ore (design section 10).
    grant(&store, &callsign, 160, 10);
    write(
        &mut send,
        &ClientMessage::Install {
            socket: "a".into(),
            facility: "sensor_array".into(),
        },
    )
    .await;
    let ServerMessage::Account { account } = read(&mut recv).await else {
        panic!("expected the account");
    };
    assert_eq!(account.credits, 110);
    assert_eq!(account.ore, 0);
    assert_eq!(
        account.outpost.sockets[0].facility.as_deref(),
        Some("sensor_array")
    );
    assert!(account.outpost.sockets[1].open, "socket B opens");
    assert_eq!(account.hangar, ["scout_rig"]);
    assert_eq!(account.launch.flown, ["scout_rig"]);
    assert_eq!(account.launch.capacity, 16);
    let ServerMessage::Contract { site, .. } = read(&mut recv).await else {
        panic!("the contract is shown again");
    };
    assert!(
        !derelict_locked(&site),
        "the sensor array locates its signal"
    );

    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Account { .. }
    ));
    let ServerMessage::Joined { site, deposits, .. } = read(&mut recv).await else {
        panic!("expected to launch");
    };
    assert!(!derelict_locked(&site));
    assert!(
        deposits
            .iter()
            .any(|deposit| deposit.material == frontier_protocol::Material::Salvage),
        "the derelict's salvage can be worked"
    );
}

#[tokio::test]
async fn trading_pays_posted_prices_and_refusals_change_nothing() {
    let (url, sha256, store) = start_server_with_store();
    let (_session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    let (callsign, _token) = sign_up(&mut send, &mut recv).await;
    read(&mut recv).await;
    grant(&store, &callsign, 0, 5);

    write(
        &mut send,
        &ClientMessage::Sell {
            good: Good::Ore,
            amount: 5,
        },
    )
    .await;
    let ServerMessage::Account { account } = read(&mut recv).await else {
        panic!("expected the account");
    };
    assert_eq!((account.credits, account.ore), (150, 0));

    write(
        &mut send,
        &ClientMessage::Install {
            socket: "b".into(),
            facility: "refinery".into(),
        },
    )
    .await;
    let ServerMessage::HomeRefused { reason } = read(&mut recv).await else {
        panic!("socket B is closed");
    };
    assert_eq!(
        reason,
        "Socket B opens when the first facility is installed."
    );
    write(
        &mut send,
        &ClientMessage::Install {
            socket: "a".into(),
            facility: "refinery".into(),
        },
    )
    .await;
    let ServerMessage::HomeRefused { reason } = read(&mut recv).await else {
        panic!("no ore left");
    };
    assert!(
        reason.starts_with("That needs 150 cr and 10 ore"),
        "{reason}"
    );
    let id = store.find(&callsign).unwrap().unwrap();
    assert_eq!(store.load(id).unwrap().credits, 150);
}

#[tokio::test]
async fn counts_too_large_to_price_are_refused_and_the_home_carries_on() {
    let (url, sha256, store) = start_server_with_store();
    let (_session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    let (callsign, _token) = sign_up(&mut send, &mut recv).await;
    read(&mut recv).await;
    let id = store.find(&callsign).unwrap().unwrap();
    let before = store.load(id).unwrap();

    // Ore sells at 16: unchecked, 2^28 of it cost nothing in a release build
    // and panicked under the store's lock in a debug one.
    let huge = [
        ClientMessage::Buy {
            good: Good::Ore,
            amount: 1 << 28,
        },
        ClientMessage::Buy {
            good: Good::Ore,
            amount: u32::MAX,
        },
        ClientMessage::Process {
            recipe: "alloy".into(),
            batches: 1 << 31,
        },
        ClientMessage::Unqueue {
            recipe: "alloy".into(),
            batches: u32::MAX,
        },
    ];
    for message in huge {
        write(&mut send, &message).await;
        assert!(
            matches!(read(&mut recv).await, ServerMessage::HomeRefused { .. }),
            "{message:?}"
        );
    }
    assert_eq!(store.load(id).unwrap(), before);

    // The session and the store are both still there.
    write(
        &mut send,
        &ClientMessage::Buy {
            good: Good::Ore,
            amount: 1,
        },
    )
    .await;
    let ServerMessage::Account { account } = read(&mut recv).await else {
        panic!("expected the account");
    };
    assert_eq!((account.credits, account.ore), (before.credits - 16, 1));
}
