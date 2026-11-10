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
