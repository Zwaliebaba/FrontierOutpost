//! Plain HTTP: serves the built client and tells it how to reach the
//! WebTransport endpoint.

use std::path::Path;

use axum::{Json, Router, routing::get};
use frontier_protocol::{CONNECT_INFO_PATH, ConnectInfo};
use tower_http::services::ServeDir;

/// The HTTP routes: [`CONNECT_INFO_PATH`] and, for every other path, the files
/// in `client_dir`.
pub fn router(connect: ConnectInfo, client_dir: &Path) -> Router {
    Router::new()
        .route(CONNECT_INFO_PATH, get(move || async move { Json(connect) }))
        .fallback_service(ServeDir::new(client_dir))
}
