//! The HTTP side serves connection details and the client files.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

use axum::body::{Body, to_bytes};
use axum::http::{Request, StatusCode};
use frontier_protocol::{CONNECT_INFO_PATH, ConnectInfo};
use frontier_server::http::router;
use tower::ServiceExt;

fn connect_info() -> ConnectInfo {
    ConnectInfo {
        web_transport_url: "https://127.0.0.1:4443/".into(),
        certificate_sha256: Some("0f".repeat(32)),
    }
}

async fn get(app: axum::Router, path: &str) -> (StatusCode, Vec<u8>) {
    let response = app
        .oneshot(Request::get(path).body(Body::empty()).unwrap())
        .await
        .unwrap();
    let status = response.status();
    let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
    (status, body.to_vec())
}

#[tokio::test]
async fn publishes_the_connection_details() {
    let dir = std::env::temp_dir();
    let (status, body) = get(router(connect_info(), &dir), CONNECT_INFO_PATH).await;
    assert_eq!(status, StatusCode::OK);
    let served: ConnectInfo = serde_json::from_slice(&body).unwrap();
    assert_eq!(served, connect_info());
}

#[tokio::test]
async fn serves_files_from_the_client_directory() {
    let dir = std::env::temp_dir().join(format!("frontier-client-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("index.html"), "<!doctype html>outpost").unwrap();

    let (status, body) = get(router(connect_info(), &dir), "/index.html").await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(body, b"<!doctype html>outpost");

    let (status, _) = get(router(connect_info(), &dir), "/missing.js").await;
    assert_eq!(status, StatusCode::NOT_FOUND);
    std::fs::remove_dir_all(&dir).unwrap();
}
