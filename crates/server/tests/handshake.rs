//! A real WebTransport client completes the opening handshake with the server.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

mod common;

use common::{connect, pinned_client, start_server};
use frontier_protocol::{ClientMessage, PROTOCOL_VERSION, ServerMessage, frame};
use web_transport_quinn::Session;
use web_transport_quinn::proto::ConnectRequest;

#[tokio::test]
async fn refuses_a_server_whose_certificate_hash_does_not_match() {
    let (url, mut sha256) = start_server();
    sha256[0] ^= 0xff;
    let result = pinned_client(sha256)
        .connect(ConnectRequest::new(url))
        .await;
    assert!(result.is_err());
}

async fn exchange(session: &Session, hello: &ClientMessage) -> ServerMessage {
    let (mut send, mut recv) = session.open_bi().await.unwrap();
    send.write_all(&frame::encode(hello).unwrap())
        .await
        .unwrap();
    let mut header = [0; frame::HEADER_BYTES];
    recv.read_exact(&mut header).await.unwrap();
    let mut body = vec![0; frame::body_length(header).unwrap()];
    recv.read_exact(&mut body).await.unwrap();
    frame::decode(&body).unwrap()
}

#[tokio::test]
async fn welcomes_a_client_on_the_current_protocol() {
    let session = connect().await;
    let reply = exchange(
        &session,
        &ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION,
        },
    )
    .await;
    assert_eq!(
        reply,
        ServerMessage::Welcome {
            protocol_version: PROTOCOL_VERSION
        }
    );
}

#[tokio::test]
async fn rejects_another_protocol_and_closes_the_session() {
    let session = connect().await;
    let reply = exchange(
        &session,
        &ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION + 1,
        },
    )
    .await;
    assert!(matches!(reply, ServerMessage::Rejected { .. }));
    let closed = tokio::time::timeout(std::time::Duration::from_secs(5), session.closed()).await;
    assert!(closed.is_ok(), "the server should close a rejected session");
}

#[tokio::test]
async fn closes_a_session_that_does_not_open_with_hello() {
    let session = connect().await;
    let (mut send, _recv) = session.open_bi().await.unwrap();
    send.write_all(&[0, 0, 0, 2, b'{', b'}']).await.unwrap();
    let closed = tokio::time::timeout(std::time::Duration::from_secs(5), session.closed()).await;
    assert!(
        closed.is_ok(),
        "the server should close a session that breaks the protocol"
    );
}
