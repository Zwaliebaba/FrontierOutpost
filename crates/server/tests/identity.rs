//! Pilots register with a passkey, sign in again with it or with their session
//! token, and must be signed in before they can play.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

mod common;

use std::time::Duration;

use common::{
    SoftPasskey, WebauthnAuthenticator, connect_to, hello, origin, read, sign_up_with,
    start_server, write,
};
use frontier_protocol::{ClientMessage, ServerMessage};

#[tokio::test]
async fn a_pilot_registers_then_signs_in_again_by_token_and_by_passkey() {
    let (url, sha256) = start_server();
    let mut key = SoftPasskey::new(true);
    let (mut send, mut recv) = {
        let (_session, mut send, mut recv) = connect_to(&url, sha256).await;
        hello(&mut send, &mut recv).await;
        let (callsign, token) = sign_up_with(&mut send, &mut recv, &mut key).await;
        assert!(matches!(
            read(&mut recv).await,
            ServerMessage::Contract { .. }
        ));

        // A reconnecting page presents its token and is not asked again.
        let (_again, mut send, mut recv) = connect_to(&url, sha256).await;
        hello(&mut send, &mut recv).await;
        write(&mut send, &ClientMessage::Resume { token }).await;
        let ServerMessage::SignedIn {
            callsign: resumed, ..
        } = read(&mut recv).await
        else {
            panic!("expected SignedIn");
        };
        assert_eq!(resumed, callsign);

        // A fresh page signs in with the passkey.
        let (session, mut send, mut recv) = connect_to(&url, sha256).await;
        hello(&mut send, &mut recv).await;
        write(
            &mut send,
            &ClientMessage::SignIn {
                callsign: callsign.to_uppercase(),
            },
        )
        .await;
        let ServerMessage::PasskeyOptions { options } = read(&mut recv).await else {
            panic!("expected passkey options");
        };
        let credential = key
            .do_authentication(origin(), serde_json::from_str(&options).unwrap())
            .unwrap();
        write(
            &mut send,
            &ClientMessage::PasskeyCredential {
                credential: serde_json::to_string(&credential).unwrap(),
            },
        )
        .await;
        let ServerMessage::SignedIn {
            callsign: signed_in,
            account,
            ..
        } = read(&mut recv).await
        else {
            panic!("expected SignedIn");
        };
        assert_eq!(signed_in, callsign);
        assert_eq!(account.credits, 100);
        std::mem::forget(session);
        (send, recv)
    };
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Contract { .. }
    ));
    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Account { .. }
    ));
}

#[tokio::test]
async fn an_unknown_token_or_callsign_is_refused_and_a_taken_callsign_too() {
    let (url, sha256) = start_server();
    let (_session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    write(
        &mut send,
        &ClientMessage::Resume {
            token: "not-a-token".into(),
        },
    )
    .await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::SignInRefused { .. }
    ));
    write(
        &mut send,
        &ClientMessage::SignIn {
            callsign: "Nobody".into(),
        },
    )
    .await;
    let ServerMessage::SignInRefused { reason } = read(&mut recv).await else {
        panic!("expected SignInRefused");
    };
    assert_eq!(reason, "No pilot is called Nobody.");

    let (callsign, _) = sign_up_with(&mut send, &mut recv, &mut SoftPasskey::new(true)).await;
    let (_other, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    write(&mut send, &ClientMessage::Register { callsign }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::SignInRefused { .. }
    ));
}

#[tokio::test]
async fn a_session_that_launches_before_signing_in_is_closed() {
    let (url, sha256) = start_server();
    let (session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    tokio::time::timeout(Duration::from_secs(5), session.closed())
        .await
        .expect("the server closes a session that breaks the protocol");
}

/// Launches a signed-up pilot and returns the session, its control stream,
/// their ship and session token.
async fn launched(
    url: &url::Url,
    sha256: [u8; 32],
) -> (
    web_transport_quinn::Session,
    web_transport_quinn::SendStream,
    web_transport_quinn::RecvStream,
    u16,
    String,
) {
    let (session, mut send, mut recv) = connect_to(url, sha256).await;
    hello(&mut send, &mut recv).await;
    let (_, token) = sign_up_with(&mut send, &mut recv, &mut SoftPasskey::new(true)).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Contract { .. }
    ));
    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Account { .. }
    ));
    let ServerMessage::Joined { ship_id, .. } = read(&mut recv).await else {
        panic!("expected Joined");
    };
    (session, send, recv, ship_id, token)
}

#[tokio::test]
async fn a_pilot_whose_connection_drops_rejoins_their_run() {
    let (url, sha256) = start_server();
    let (session, _send, _recv, ship, token) = launched(&url, sha256).await;
    session.close(0, b"dropped");
    tokio::time::sleep(Duration::from_millis(300)).await;

    let (again, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    write(&mut send, &ClientMessage::Resume { token }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::SignedIn { .. }
    ));
    let ServerMessage::Joined { ship_id, site, .. } = read(&mut recv).await else {
        panic!("expected to rejoin the run, not a new contract");
    };
    assert_eq!(ship_id, ship, "the same ship");
    assert_eq!(site.pockets.len(), 4, "with the site to draw it");
    let bytes = tokio::time::timeout(Duration::from_secs(2), again.read_datagram())
        .await
        .unwrap()
        .unwrap();
    let snapshot = frontier_protocol::snapshot::Snapshot::decode(&bytes).unwrap();
    assert!(snapshot.entities.iter().any(|e| e.id == ship));
}

#[tokio::test]
async fn signing_in_on_another_page_closes_the_first() {
    let (url, sha256) = start_server();
    let (first, _send, mut first_recv, ship, token) = launched(&url, sha256).await;

    let (_second, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    write(&mut send, &ClientMessage::Resume { token }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::SignedIn { .. }
    ));
    let ServerMessage::Joined { ship_id, .. } = read(&mut recv).await else {
        panic!("the second page takes over the run");
    };
    assert_eq!(ship_id, ship);

    // The first page is told why, and its session is closed so it stays closed.
    let told = tokio::time::timeout(Duration::from_secs(2), read(&mut first_recv))
        .await
        .unwrap();
    assert_eq!(
        told,
        ServerMessage::SignInRefused {
            reason: "Signed in on another page.".into()
        }
    );
    // The server gives a displaced page a moment to close itself first.
    let closed = tokio::time::timeout(Duration::from_secs(5), first.closed())
        .await
        .unwrap();
    assert!(
        matches!(
            &closed,
            web_transport_quinn::SessionError::WebTransportError(
                web_transport_quinn::WebTransportError::Closed(code, reason)
            ) if *code == frontier_server::transport::SIGNED_IN_ELSEWHERE
                && reason == "signed in elsewhere"
        ),
        "{closed:?}"
    );
}
