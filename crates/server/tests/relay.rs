//! The development relay slows and degrades a real WebTransport session
//! without breaking it.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

mod common;

use std::time::{Duration, Instant};

use common::{connect_to, hello, read, start_server, write};
use frontier_protocol::{ClientMessage, ServerMessage};
use frontier_server::relay::{self, Impairment};

async fn through_relay(impairment: Impairment) -> (url::Url, [u8; 32]) {
    let (url, sha256) = start_server();
    let upstream = format!("127.0.0.1:{}", url.port().unwrap())
        .parse()
        .unwrap();
    let relay = relay::start("127.0.0.1:0".parse().unwrap(), upstream, impairment)
        .await
        .unwrap();
    (format!("https://{relay}/").parse().unwrap(), sha256)
}

#[tokio::test]
async fn adds_the_delay_to_every_round_trip() {
    let delay = Duration::from_millis(60);
    let (url, sha256) = through_relay(Impairment {
        delay,
        jitter: Duration::ZERO,
        loss: 0.0,
    })
    .await;
    let (_session, mut send, mut recv) = connect_to(&url, sha256).await;
    let started = Instant::now();
    assert!(matches!(
        hello(&mut send, &mut recv).await,
        ServerMessage::Welcome { .. }
    ));
    assert!(started.elapsed() >= delay * 2, "{:?}", started.elapsed());
}

#[tokio::test]
async fn a_session_survives_loss_and_jitter() {
    let (url, sha256) = through_relay(Impairment {
        delay: Duration::from_millis(20),
        jitter: Duration::from_millis(20),
        loss: 0.1,
    })
    .await;
    let (session, mut send, mut recv) = connect_to(&url, sha256).await;
    assert!(matches!(
        hello(&mut send, &mut recv).await,
        ServerMessage::Welcome { .. }
    ));
    common::sign_up(&mut send, &mut recv).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Contract { .. }
    ));
    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Account { .. }
    ));
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Joined { .. }
    ));
    // Snapshots keep arriving, minus the ones the relay drops.
    let mut received = 0;
    let deadline = tokio::time::Instant::now() + Duration::from_secs(1);
    while let Ok(Ok(_)) = tokio::time::timeout_at(deadline, session.read_datagram()).await {
        received += 1;
    }
    assert!(received >= 10, "only {received} snapshots in a second");
}
