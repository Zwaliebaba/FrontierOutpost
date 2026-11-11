use frontier_protocol::{ConnectInfo, PROTOCOL_VERSION};
use frontier_server::auth::Auth;
use frontier_server::cert::{CertificateFiles, DevCertificate, LoadedCertificate};
use frontier_server::config::{self, Config};
use frontier_server::expedition::Expedition;
use frontier_server::ledger::{Npc, Party, Reason, Transfer};
use frontier_server::log::Lines;
use frontier_server::runs::Runs;
use frontier_server::store::Store;
use frontier_server::transport::Identity;
use frontier_server::{http, relay, report, transport};
use std::time::Duration;
use time::OffsetDateTime;
use tracing::{info, warn};
use tracing_subscriber::EnvFilter;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Logging never waits for whoever reads standard output: see `log`.
    let log = Lines::to(std::io::stdout())?;
    let _flushed = log.flush_on_drop();
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()))
        .with_writer(log)
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
    let stop = shutdown_signal();
    // With certificate files from an authority (FRONTIER_TLS_CERT), browsers
    // trust the certificate and need no hash; otherwise one is generated.
    let (server, certificate_sha256) = match &config.tls {
        Some(files) => {
            let loaded = wait_for_certificate(files).await?;
            let endpoint = transport::endpoint(
                config.web_transport_addr,
                loaded.chain,
                loaded.key,
                transport::IDLE_TIMEOUT,
            )?;
            tokio::spawn(transport::renew_certificate(
                endpoint.clone(),
                files.clone(),
                transport::IDLE_TIMEOUT,
                RENEWAL_CHECK,
            ));
            info!(chain = %files.chain.display(), "WebTransport certificate from files");
            (web_transport_quinn::Server::new(endpoint), None)
        }
        None => {
            let host = config.web_transport_addr.ip().to_string();
            let public_host = config
                .public_web_transport_addr
                .map_or_else(|| host.clone(), |addr| addr.ip().to_string());
            let cert = DevCertificate::generate(
                &["localhost", &host, &public_host],
                OffsetDateTime::now_utc(),
            )?;
            let server = transport::bind(config.web_transport_addr, &cert)?;
            (server, Some(cert.sha256_hex()))
        }
    };
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
    if config.tls.is_none() && browser_addr.ip().is_unspecified() {
        warn!(
            %browser_addr,
            "browsers cannot dial an unspecified address: set FRONTIER_PUBLIC_WEBTRANSPORT_ADDR"
        );
    }

    let connect = ConnectInfo {
        web_transport_url: config.web_transport_url(listening_addr),
        certificate_sha256,
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
            runs: runs.clone(),
            crews: frontier_server::crews::Crews::default(),
        },
    ));
    axum::serve(listener, http::router(connect, &config.client_dir))
        .with_graceful_shutdown(stop)
        .await?;
    save_runs(&runs).await;
    info!("stopped");
    Ok(())
}

/// How often the server looks for a renewed certificate. Caddy renews a month
/// before expiry, so hourly is ample.
const RENEWAL_CHECK: Duration = Duration::from_secs(60 * 60);

/// How long a starting server waits for its authority's first certificate:
/// Caddy obtains it once the name resolves to this host.
const CERTIFICATE_WAIT: Duration = Duration::from_secs(10 * 60);

/// Waits for the certificate files to appear, then reads them.
async fn wait_for_certificate(
    files: &CertificateFiles,
) -> Result<LoadedCertificate, Box<dyn std::error::Error>> {
    let started = std::time::Instant::now();
    let mut said = false;
    loop {
        if files.modified().is_some() {
            return Ok(files.load()?);
        }
        if started.elapsed() >= CERTIFICATE_WAIT {
            return Err(format!(
                "no certificate at {} after {} minutes",
                files.chain.display(),
                CERTIFICATE_WAIT.as_secs() / 60
            )
            .into());
        }
        if !said {
            info!(chain = %files.chain.display(), "waiting for the certificate");
            said = true;
        }
        tokio::time::sleep(Duration::from_secs(2)).await;
    }
}

/// How long a stopping server gives its instances to save. `docker stop`
/// kills the process ten seconds after asking.
const STOP_GRACE: Duration = Duration::from_secs(5);

/// Checkpoints every live run before the process ends, so a restart restores
/// each ship where it was when the server stopped (design section 6).
async fn save_runs(runs: &Runs) {
    let instances = runs.expeditions();
    let count = instances.len();
    let saved = tokio::time::timeout(STOP_GRACE, async {
        for instance in instances {
            instance.stop().await;
        }
    })
    .await;
    match saved {
        Ok(()) => info!(runs = count, "runs saved"),
        Err(_) => warn!(
            runs = count,
            "not every run was saved in time; the rest restart from their last checkpoint"
        ),
    }
}

/// Starts listening for the request to stop, and returns what resolves when
/// it comes: Ctrl+C, or on Unix SIGTERM, which is how `docker stop` asks. A
/// process running as a container's PID 1 ignores SIGTERM unless it handles
/// it, and is killed after a timeout.
///
/// The SIGTERM handler is installed here, not when the result is first
/// awaited. Until it is, the signal kills the process outright, so this is
/// called before the server says it is open.
fn shutdown_signal() -> impl Future<Output = ()> {
    #[cfg(unix)]
    let terminate = {
        use tokio::signal::unix::{SignalKind, signal};
        signal(SignalKind::terminate())
            .inspect_err(
                |error| warn!(%error, "cannot listen for SIGTERM; only Ctrl+C stops the server"),
            )
            .ok()
    };
    async move {
        #[cfg(unix)]
        if let Some(mut terminate) = terminate {
            tokio::select! {
                _ = tokio::signal::ctrl_c() => {}
                _ = terminate.recv() => {}
            }
            return;
        }
        let _ = tokio::signal::ctrl_c().await;
    }
}
