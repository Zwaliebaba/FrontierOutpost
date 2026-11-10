//! The WebTransport endpoint: accepts sessions and runs each one.
//!
//! A session opens with one bidirectional stream, the control stream, which
//! carries framed JSON messages ([`frontier_protocol::frame`]). The first
//! client message must be `Hello`; the server answers with [`handshake`].

use std::hash::{BuildHasher, RandomState};
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};

use frontier_protocol::input::InputDatagram;
use frontier_protocol::{ClientMessage, ServerMessage, frame};
use frontier_sim::run::Options;
use frontier_sim::site::{FRONTIER_SITE, SiteTemplate, ThreatTier};
use frontier_sim::world::ShipId;
use tokio::sync::mpsc;
use tracing::{debug, info, warn};
use web_transport_quinn::{RecvStream, SendStream, Server, Session, crypto, quinn};

use crate::cert::DevCertificate;
use crate::expedition::{Expedition, LaunchOrder, PilotEvent};
use crate::{handshake, wire};

/// What the server offers every pilot, and how runs launch.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Settings {
    /// The site every contract offers.
    pub template: SiteTemplate,
    /// Tiers a pilot may choose, up to 3. Tiers open with facilities (design
    /// section 10), which arrive in plan M5; until then this is a switch.
    pub open_tiers: u32,
    /// A fixed seed for every contract, for tests; random otherwise.
    pub seed: Option<u32>,
    /// Replaces the opening threat, for benchmarks.
    pub opening_threat: Option<u32>,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            template: FRONTIER_SITE,
            open_tiers: 1,
            seed: None,
            opening_threat: None,
        }
    }
}

impl Settings {
    fn next_seed(&self) -> u32 {
        // A per-process random hasher of a varying value: a seed nobody can guess.
        self.seed.unwrap_or_else(|| {
            let nanos = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_or(0, |since| since.as_nanos());
            RandomState::new().hash_one(nanos) as u32
        })
    }
}

/// Why a session ended other than by the client leaving normally.
#[derive(Debug, thiserror::Error)]
pub enum SessionError {
    #[error(transparent)]
    Session(#[from] web_transport_quinn::SessionError),
    #[error("could not read from the control stream: {0}")]
    Read(#[from] web_transport_quinn::ReadExactError),
    #[error("could not write to the control stream: {0}")]
    Write(#[from] web_transport_quinn::WriteError),
    #[error(transparent)]
    Frame(#[from] frame::FrameError),
    #[error("the control stream was closed")]
    Closed,
    #[error("the client sent a message out of turn")]
    Unexpected,
}

/// Session close code for a client that broke the protocol.
const PROTOCOL_VIOLATION: u32 = 1;
/// Session close code for a client whose protocol version is not supported.
const VERSION_REJECTED: u32 = 2;

/// How long a connection may go without hearing from the client before the
/// server drops it. Browsers do not always close a session when a page closes,
/// and QUIC's usual 30 s would leave a ghost ship that long.
pub const IDLE_TIMEOUT: Duration = Duration::from_secs(10);

/// How often the server pings a client. A pilot at home between runs sends
/// nothing, but their browser acknowledges each ping, which keeps the
/// connection alive; a closed page stops acknowledging and is dropped after
/// [`IDLE_TIMEOUT`]. A third of the timeout leaves room for a lost ping or two.
fn keep_alive(idle_timeout: Duration) -> Duration {
    idle_timeout / 3
}

/// Why the WebTransport endpoint could not be bound.
#[derive(Debug, thiserror::Error)]
pub enum BindError {
    #[error("could not configure TLS: {0}")]
    Tls(#[from] rustls::Error),
    #[error("TLS configuration is unusable for QUIC: {0}")]
    Quic(#[from] quinn::crypto::rustls::NoInitialCipherSuite),
    #[error("idle timeout out of range: {0}")]
    IdleTimeout(#[from] quinn::VarIntBoundsExceeded),
    #[error("could not bind the WebTransport socket: {0}")]
    Io(#[from] std::io::Error),
}

/// Binds the WebTransport endpoint with [`IDLE_TIMEOUT`].
///
/// # Errors
/// Fails if the address cannot be bound or the certificate is unusable.
pub fn bind(addr: SocketAddr, cert: &DevCertificate) -> Result<Server, BindError> {
    bind_with_idle_timeout(addr, cert, IDLE_TIMEOUT)
}

/// Binds the WebTransport endpoint, dropping sessions silent for `idle_timeout`.
///
/// This builds the QUIC endpoint itself, as `web_transport_quinn::ServerBuilder`
/// does, because the builder offers no way to set the idle timeout.
///
/// # Errors
/// Fails if the address cannot be bound or the certificate is unusable.
pub fn bind_with_idle_timeout(
    addr: SocketAddr,
    cert: &DevCertificate,
    idle_timeout: Duration,
) -> Result<Server, BindError> {
    let mut tls = rustls::ServerConfig::builder_with_provider(crypto::default_provider())
        .with_protocol_versions(&[&rustls::version::TLS13])?
        .with_no_client_auth()
        .with_single_cert(cert.chain.clone(), cert.key.clone_key())?;
    tls.alpn_protocols = vec![web_transport_quinn::ALPN.as_bytes().to_vec()];

    let mut transport = quinn::TransportConfig::default();
    transport.max_idle_timeout(Some(idle_timeout.try_into()?));
    transport.keep_alive_interval(Some(keep_alive(idle_timeout)));
    let mut config = quinn::ServerConfig::with_crypto(Arc::new(
        quinn::crypto::rustls::QuicServerConfig::try_from(tls)?,
    ));
    config.transport_config(Arc::new(transport));
    Ok(Server::new(quinn::Endpoint::server(config, addr)?))
}

/// Accepts sessions until the endpoint is closed, running each on its own task.
pub async fn serve(mut server: Server, settings: Settings) {
    while let Some(request) = server.accept().await {
        let peer = request.conn().remote_address();
        tokio::spawn(async move {
            match request.ok().await {
                Ok(session) => {
                    info!(%peer, "session opened");
                    match run_session(&session, settings).await {
                        Ok(()) => info!(%peer, "session closed"),
                        Err(error) => {
                            warn!(%peer, %error, "session ended");
                            session.close(PROTOCOL_VIOLATION, b"protocol violation");
                        }
                    }
                }
                Err(error) => debug!(%peer, %error, "WebTransport handshake failed"),
            }
        });
    }
}

async fn run_session(session: &Session, settings: Settings) -> Result<(), SessionError> {
    let (mut send, mut recv) = session.accept_bi().await?;
    let hello: ClientMessage = read_frame(&mut recv).await?;
    let reply = handshake(&hello);
    write_frame(&mut send, &reply).await?;
    if let ServerMessage::Rejected { .. } = reply {
        let _ = send.finish();
        session.close(VERSION_REJECTED, b"protocol version not supported");
        return Ok(());
    }

    let (events_tx, events) = mpsc::unbounded_channel();
    let mut pilot = Pilot {
        session,
        settings,
        send,
        events_tx,
        flying: None,
        seed: 0,
        offered_at: Instant::now(),
        home_since: None,
    };
    pilot.offer().await?;
    let result = pilot.fly(recv, events).await;
    match &pilot.flying {
        Some((expedition, ship)) => expedition.leave(*ship),
        None => pilot.log_unloading(),
    }
    result
}

/// One connected pilot: at home with a contract on offer, or flying a run.
struct Pilot<'a> {
    session: &'a Session,
    settings: Settings,
    send: SendStream,
    events_tx: mpsc::UnboundedSender<PilotEvent>,
    /// The run being flown and the pilot's ship in it.
    flying: Option<(Expedition, ShipId)>,
    /// The seed of the contract on offer.
    seed: u32,
    /// When the contract was offered: preparation starts here.
    offered_at: Instant,
    /// When the pilot's last run ended: unloading starts here.
    home_since: Option<Instant>,
}

impl Pilot<'_> {
    /// Offers a new contract.
    async fn offer(&mut self) -> Result<(), SessionError> {
        self.seed = self.settings.next_seed();
        self.offered_at = Instant::now();
        let contract = wire::contract(self.settings.template, self.seed, self.settings.open_tiers);
        write_frame(&mut self.send, &contract).await
    }

    /// Logs the time since the last run ended: design section 2's unloading
    /// and decisions, which in Stage 1's first build runs into preparation.
    fn log_unloading(&mut self) {
        if let Some(since) = self.home_since.take() {
            info!(target: "frontier::run", seconds = since.elapsed().as_secs(), "unloading");
        }
    }

    /// Passes the pilot's datagrams and control messages to their run, and the
    /// run's news to the pilot, until the session ends.
    async fn fly(
        &mut self,
        mut recv: RecvStream,
        mut events: mpsc::UnboundedReceiver<PilotEvent>,
    ) -> Result<(), SessionError> {
        // Frames are read on their own task: reading one is not cancel-safe, so
        // it must not race other events in the select below.
        let (frames_tx, mut frames) = mpsc::unbounded_channel();
        tokio::spawn(async move {
            loop {
                let frame = read_frame::<ClientMessage>(&mut recv).await;
                let failed = frame.is_err();
                if frames_tx.send(frame).is_err() || failed {
                    return;
                }
            }
        });

        loop {
            tokio::select! {
                datagram = self.session.read_datagram() => match datagram {
                    Ok(bytes) => match (InputDatagram::decode(&bytes), &self.flying) {
                        (Ok(input), Some((expedition, ship))) => expedition.input(*ship, input),
                        // Inputs sent from home go nowhere.
                        (Ok(_), None) => {}
                        // A malformed datagram is dropped, as a lost one would be.
                        (Err(error), _) => debug!(%error, "datagram ignored"),
                    },
                    // The session closed while reading: the client left.
                    Err(_) => return Ok(()),
                },
                frame = frames.recv() => match frame {
                    Some(Ok(message)) => self.handle(message).await?,
                    // The client finished the control stream: it is leaving.
                    Some(Err(SessionError::Read(_))) | None => return Ok(()),
                    Some(Err(error)) => return Err(error),
                },
                Some(event) = events.recv() => match event {
                    PilotEvent::Message(message) => write_frame(&mut self.send, &message).await?,
                    PilotEvent::RunOver(summary) => {
                        self.flying = None;
                        self.home_since = Some(Instant::now());
                        write_frame(&mut self.send, &ServerMessage::RunOver { summary }).await?;
                        self.offer().await?;
                    }
                },
                _ = self.session.closed() => return Ok(()),
            }
        }
    }

    async fn handle(&mut self, message: ClientMessage) -> Result<(), SessionError> {
        let flying = self.flying.as_ref().map(|(e, s)| (e.clone(), *s));
        match (message, flying) {
            (ClientMessage::Launch { tier }, None) => self.launch(tier).await?,
            (ClientMessage::Lock { target }, Some((expedition, ship))) => {
                expedition.lock(ship, target.map(ShipId));
            }
            (ClientMessage::Extract { on }, Some((expedition, ship))) => {
                expedition.extract(ship, on);
            }
            (ClientMessage::Vent, Some((expedition, ship))) => expedition.vent(ship),
            (ClientMessage::Scan, Some((expedition, ship))) => expedition.scan(ship),
            // Actions sent just as a run ends arrive too late to matter.
            (
                ClientMessage::Lock { .. }
                | ClientMessage::Extract { .. }
                | ClientMessage::Vent
                | ClientMessage::Scan,
                None,
            ) => {}
            (ClientMessage::Launch { .. }, Some(_)) | (ClientMessage::Hello { .. }, _) => {
                return Err(SessionError::Unexpected);
            }
        }
        Ok(())
    }

    async fn launch(&mut self, tier: u32) -> Result<(), SessionError> {
        let Some(tier) =
            ThreatTier::from_number(tier).filter(|tier| tier.number() <= self.settings.open_tiers)
        else {
            let reason = format!("tier {tier} is not open");
            return write_frame(&mut self.send, &ServerMessage::LaunchRefused { reason }).await;
        };
        self.log_unloading();
        info!(
            target: "frontier::run",
            run = self.seed,
            seconds = self.offered_at.elapsed().as_secs(),
            "preparation"
        );
        let order = LaunchOrder {
            template: self.settings.template,
            seed: self.seed,
            tier,
            options: Options {
                opening_threat: self.settings.opening_threat,
            },
        };
        let launched = Expedition::launch(order, self.session.clone(), self.events_tx.clone());
        write_frame(&mut self.send, &launched.joined).await?;
        self.flying = Some((launched.expedition, launched.ship));
        Ok(())
    }
}

/// Reads one framed message from a stream.
///
/// # Errors
/// Fails if the stream ends, the frame is too large or the body is not a `T`.
pub async fn read_frame<T: serde::de::DeserializeOwned>(
    recv: &mut RecvStream,
) -> Result<T, SessionError> {
    let mut header = [0; frame::HEADER_BYTES];
    recv.read_exact(&mut header).await?;
    let mut body = vec![0; frame::body_length(header)?];
    recv.read_exact(&mut body).await?;
    Ok(frame::decode(&body)?)
}

/// Writes one framed message to a stream.
///
/// # Errors
/// Fails if the message cannot be encoded or the stream is closed.
pub async fn write_frame<T: serde::Serialize>(
    send: &mut SendStream,
    message: &T,
) -> Result<(), SessionError> {
    send.write_all(&frame::encode(message)?).await?;
    Ok(())
}
