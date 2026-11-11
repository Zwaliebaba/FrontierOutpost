use frontier_protocol::{ConnectInfo, PROTOCOL_VERSION};
use frontier_server::auth::Auth;
use frontier_server::cert::DevCertificate;
use frontier_server::config::{self, Config};
use frontier_server::expedition::Expedition;
use frontier_server::ledger::{Npc, Party, Reason, Transfer};
use frontier_server::runs::Runs;
use frontier_server::store::Store;
use frontier_server::transport::Identity;
use frontier_server::{http, relay, report, transport};
use time::OffsetDateTime;
use tracing::{info, warn};
use tracing_subscriber::EnvFilter;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()))
        .init();

    let config = Config::from_env()?;
    // `frontier-server wipe`: Stage 1 data may be wiped between builds.
    // `frontier-server report`: the Stage 1 gate report from the saves.
    // `frontier-server grant`: stock for an existing pilot, for testing.
    match std::env::args().nth(1).as_deref() {
        None => {}
        Some("wipe") => {
            let store = Store::wipe(&config.data_path)?;
            info!(data = %config.data_path.display(), world = store.world(), "wiped: an empty world");
            return Ok(());
        }
        Some("report") => {
            // `frontier-server report [greybox.json ...]`: the Stage 1 gates.
            let store = Store::open(&config.data_path)?;
            let greybox = std::env::args()
                .skip(2)
                .map(
                    |path| -> Result<report::Greybox, Box<dyn std::error::Error>> {
                        let text = std::fs::read_to_string(&path)
                            .map_err(|error| format!("{path}: {error}"))?;
                        Ok(serde_json::from_str(&text)
                            .map_err(|error| format!("{path}: {error}"))?)
                    },
                )
                .collect::<Result<Vec<_>, _>>()?;
            print!("{}", report::compute(&store.events()?, &greybox).markdown());
            return Ok(());
        }
        Some("grant") => {
            // `frontier-server grant <callsign> 1000cr,40ore`: tops up a pilot
            // who already exists, as FRONTIER_GRANT does for new ones.
            const USAGE: &str =
                "usage: frontier-server grant <callsign> <stock, such as 1000cr,40ore>";
            let mut args = std::env::args().skip(2);
            let (Some(callsign), Some(stock), None) = (args.next(), args.next(), args.next())
            else {
                return Err(USAGE.into());
            };
            let stock = config::parse_grant(&stock)
                .filter(|stock| !stock.is_empty())
                .ok_or_else(|| format!("{stock:?} is not stock such as 1000cr,40ore; {USAGE}"))?;
            let store = Store::open(&config.data_path)?;
            let pilot = store
                .find(&callsign)?
                .ok_or_else(|| format!("no pilot is called {callsign:?}"))?;
            let transfers: Vec<Transfer> = stock
                .iter()
                .map(|&(item, amount)| Transfer {
                    item,
                    amount,
                    from: Party::Npc(Npc::Grant),
                    to: Party::Account(pilot),
                    reason: Reason::TestGrant,
                    run: None,
                })
                .collect();
            store.transfer(&transfers)?;
            info!(
                callsign,
                ?stock,
                "granted: the page shows it after a reload"
            );
            return Ok(());
        }
        Some(other) => {
            return Err(format!(
                "unknown command {other:?}; the commands are wipe, report and grant"
            )
            .into());
        }
    }
    let host = config.web_transport_addr.ip().to_string();
    let public_host = config
        .public_web_transport_addr
        .map_or_else(|| host.clone(), |addr| addr.ip().to_string());
    let cert = DevCertificate::generate(
        &["localhost", &host, &public_host],
        OffsetDateTime::now_utc(),
    )?;
    let server = transport::bind(config.web_transport_addr, &cert)?;
    let web_transport_addr = server.local_addr()?;

    // With an impairment set, browsers connect through the relay instead.
    let listening_addr = if config.impairment.is_active() {
        let relay_bind = (web_transport_addr.ip(), 0).into();
        let relay_addr = relay::start(relay_bind, web_transport_addr, config.impairment).await?;
        warn!(
            delay = ?config.impairment.delay,
            jitter = ?config.impairment.jitter,
            loss = config.impairment.loss,
            %relay_addr,
            "impaired link: every packet is delayed, and some dropped, in each direction"
        );
        relay_addr
    } else {
        web_transport_addr
    };
    let browser_addr = config.browser_addr(listening_addr);
    if browser_addr.ip().is_unspecified() {
        warn!(
            %browser_addr,
            "browsers cannot dial an unspecified address: set FRONTIER_PUBLIC_WEBTRANSPORT_ADDR"
        );
    }

    let connect = ConnectInfo {
        web_transport_url: format!("https://{browser_addr}/"),
        certificate_sha256: Some(cert.sha256_hex()),
    };
    let listener = tokio::net::TcpListener::bind(config.http_addr).await?;
    let http_addr = listener.local_addr()?;

    // Passkeys are bound to the origin, and an IP address cannot be one, so the
    // game is opened at localhost (or the configured origin), not 127.0.0.1.
    let origin = config.origin(http_addr);
    let auth = Auth::new(&origin)?;
    let store = Store::open(&config.data_path)?.with_grant(&config.grant);
    if !config.grant.is_empty() {
        warn!(grant = ?config.grant, "every new pilot starts with extra stock");
    }
    info!(data = %config.data_path.display(), world = store.world(), "saves");

    info!(protocol = PROTOCOL_VERSION, %web_transport_addr, %browser_addr, "WebTransport listening");
    info!(client = %config.client_dir.display(), %http_addr, "open {origin}");

    info!(
        site = config.settings.template.name,
        tiers = config.settings.open_tiers,
        "contracts"
    );
    // Runs that were going when the server stopped come back from their last
    // checkpoint, waiting for their pilots (design section 6).
    let runs = Runs::default();
    for run in store.checkpoints()? {
        Expedition::resume(run, store.clone(), runs.clone());
    }
    tokio::spawn(transport::serve(
        server,
        config.settings,
        Identity {
            store,
            auth,
            runs,
            crews: frontier_server::crews::Crews::default(),
        },
    ));
    axum::serve(listener, http::router(connect, &config.client_dir))
        .with_graceful_shutdown(shutdown_signal())
        .await?;
    info!("stopped");
    Ok(())
}

/// Resolves when the process is asked to stop: Ctrl+C, or on Unix SIGTERM,
/// which is how `docker stop` asks. A process running as a container's PID 1
/// ignores SIGTERM unless it handles it, and is killed after a timeout.
async fn shutdown_signal() {
    #[cfg(unix)]
    {
        use tokio::signal::unix::{SignalKind, signal};
        match signal(SignalKind::terminate()) {
            Ok(mut terminate) => {
                tokio::select! {
                    _ = tokio::signal::ctrl_c() => {}
                    _ = terminate.recv() => {}
                }
                return;
            }
            Err(error) => warn!(%error, "cannot listen for SIGTERM; only Ctrl+C stops the server"),
        }
    }
    let _ = tokio::signal::ctrl_c().await;
}
