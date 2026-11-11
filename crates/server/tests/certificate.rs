//! A WebTransport certificate from an authority (plan 7.3): browsers trust it
//! without a hash, and a renewal reaches new sessions while the server runs.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

use std::time::Duration;

use frontier_protocol::ServerMessage;
use frontier_server::auth::Auth;
use frontier_server::store::Store;
use frontier_server::transport::{self, Identity};
use web_transport_quinn::proto::ConnectRequest;

mod common;

fn scratch(name: &str) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("frontier-tls-{}-{name}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// Whether a client trusting only `root` completes the handshake at `url`.
async fn reaches(url: &url::Url, root: rustls::pki_types::CertificateDer<'static>) -> bool {
    let Ok(session) = common::trusting_client(root)
        .connect(ConnectRequest::new(url.clone()))
        .await
    else {
        return false;
    };
    let (mut send, mut recv) = session.open_bi().await.unwrap();
    matches!(
        common::hello(&mut send, &mut recv).await,
        ServerMessage::Welcome { .. }
    )
}

#[tokio::test]
async fn presents_the_authoritys_certificate_and_its_renewal() {
    let dir = scratch("renewal");
    let (files, first) = common::issue_certificate(&dir, &["127.0.0.1"]);
    let loaded = files.load().unwrap();
    let endpoint = transport::endpoint(
        "127.0.0.1:0".parse().unwrap(),
        loaded.chain,
        loaded.key,
        transport::IDLE_TIMEOUT,
    )
    .unwrap();
    let url: url::Url = format!("https://{}/", endpoint.local_addr().unwrap())
        .parse()
        .unwrap();
    tokio::spawn(transport::renew_certificate(
        endpoint.clone(),
        files.clone(),
        transport::IDLE_TIMEOUT,
        Duration::from_millis(50),
    ));
    let identity = Identity {
        store: Store::in_memory().unwrap(),
        auth: Auth::new(&common::origin()).unwrap(),
        runs: frontier_server::runs::Runs::default(),
        crews: frontier_server::crews::Crews::default(),
    };
    tokio::spawn(transport::serve(
        web_transport_quinn::Server::new(endpoint),
        common::test_settings(),
        identity,
    ));

    // Trusted as a browser trusts a public authority: no hash.
    assert!(reaches(&url, first.clone()).await);

    // The authority renews: new files, from what to the client is a new issuer.
    tokio::time::sleep(Duration::from_millis(20)).await;
    let renewed_dir = scratch("renewed");
    let (renewed, second) = common::issue_certificate(&renewed_dir, &["127.0.0.1"]);
    std::fs::copy(&renewed.chain, &files.chain).unwrap();
    std::fs::copy(&renewed.key, &files.key).unwrap();
    let mut presented = false;
    for _ in 0..60 {
        if reaches(&url, second.clone()).await {
            presented = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    assert!(presented, "new sessions get the renewed certificate");
    assert!(!reaches(&url, first).await, "and no longer the old one");
    let _ = std::fs::remove_dir_all(&dir);
    let _ = std::fs::remove_dir_all(&renewed_dir);
}
