use frontier_protocol::{ConnectInfo, PROTOCOL_VERSION};
use frontier_server::cert::DevCertificate;
use frontier_server::config::Config;
use frontier_server::{http, transport};
use time::OffsetDateTime;
use tracing::info;
use tracing_subscriber::EnvFilter;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()))
        .init();

    let config = Config::from_env()?;
    let host = config.web_transport_addr.ip().to_string();
    let cert = DevCertificate::generate(&["localhost", &host], OffsetDateTime::now_utc())?;
    let server = transport::bind(config.web_transport_addr, &cert)?;
    let web_transport_addr = server.local_addr()?;

    let connect = ConnectInfo {
        web_transport_url: format!("https://{web_transport_addr}/"),
        certificate_sha256: Some(cert.sha256_hex()),
    };
    let listener = tokio::net::TcpListener::bind(config.http_addr).await?;
    let http_addr = listener.local_addr()?;

    info!(protocol = PROTOCOL_VERSION, %web_transport_addr, "WebTransport listening");
    info!(client = %config.client_dir.display(), "open http://{http_addr}/");

    tokio::spawn(transport::serve(server));
    axum::serve(listener, http::router(connect, &config.client_dir))
        .with_graceful_shutdown(async {
            let _ = tokio::signal::ctrl_c().await;
        })
        .await?;
    Ok(())
}
