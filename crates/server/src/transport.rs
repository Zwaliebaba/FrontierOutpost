//! The WebTransport endpoint: accepts sessions and runs each one.
//!
//! A session opens with one bidirectional stream, the control stream, which
//! carries framed JSON messages ([`frontier_protocol::frame`]). The first
//! client message must be `Hello`; the server answers with [`handshake`].

use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Duration;

use frontier_protocol::input::InputDatagram;
use frontier_protocol::{ClientMessage, ServerMessage, frame};
use frontier_sim::TICKS_PER_SECOND;
use frontier_sim::world::ShipId;
use tokio::sync::mpsc;
use tracing::{debug, info, warn};
use web_transport_quinn::{RecvStream, SendStream, Server, Session, crypto, quinn};

use crate::cert::DevCertificate;
use crate::expedition::{Expedition, PilotEvent};
use crate::handshake;

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
/// Session close code when the expedition has no room.
const EXPEDITION_FULL: u32 = 3;
/// How long the server waits for a turned-away client to close the session itself.
const PARTING_GRACE: Duration = Duration::from_secs(2);

/// How long a session may go silent before the server drops it. A pilot's
/// client sends inputs 20 times a second, so only a client that has gone away
/// is ever this quiet. Browsers do not always close a session when a page
/// closes, and QUIC's usual 30 s would leave a ghost ship that long.
pub const IDLE_TIMEOUT: Duration = Duration::from_secs(10);

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
    let mut config = quinn::ServerConfig::with_crypto(Arc::new(
        quinn::crypto::rustls::QuicServerConfig::try_from(tls)?,
    ));
    config.transport_config(Arc::new(transport));
    Ok(Server::new(quinn::Endpoint::server(config, addr)?))
}

/// Accepts sessions until the endpoint is closed, running each on its own
/// task and joining it to `expedition`.
pub async fn serve(mut server: Server, expedition: Expedition) {
    while let Some(request) = server.accept().await {
        let peer = request.conn().remote_address();
        let expedition = expedition.clone();
        tokio::spawn(async move {
            match request.ok().await {
                Ok(session) => {
                    info!(%peer, "session opened");
                    match run_session(&session, &expedition).await {
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

async fn run_session(session: &Session, expedition: &Expedition) -> Result<(), SessionError> {
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
    let Some(ship) = expedition.join(session.clone(), events_tx.clone()).await else {
        write_frame(&mut send, &ServerMessage::ExpeditionFull).await?;
        let _ = send.finish();
        // Closing at once can discard the message before it arrives, so give the
        // client a moment to read it and close first.
        let _ = tokio::time::timeout(PARTING_GRACE, session.closed()).await;
        session.close(EXPEDITION_FULL, b"expedition full");
        return Ok(());
    };
    write_frame(&mut send, &joined(expedition, ship)).await?;
    let mut pilot = Pilot {
        session,
        expedition,
        send,
        ship: Some(ship),
        events_tx,
    };
    let result = pilot.fly(recv, events).await;
    if let Some(ship) = pilot.ship {
        expedition.leave(ship);
    }
    result
}

fn joined(expedition: &Expedition, ship: ShipId) -> ServerMessage {
    ServerMessage::Joined {
        ship_id: ship.0,
        tick_rate: TICKS_PER_SECOND,
        obstacles: expedition
            .obstacles()
            .iter()
            .map(|o| frontier_protocol::ObstacleInfo {
                x: o.x,
                z: o.z,
                radius: o.radius,
            })
            .collect(),
        weapon_range: frontier_sim::profile::ShipKind::Pilot
            .profile()
            .weapon
            .range,
    }
}

/// One connected pilot, in the expedition or at home after being destroyed.
struct Pilot<'a> {
    session: &'a Session,
    expedition: &'a Expedition,
    send: SendStream,
    /// The ship being flown, or `None` while the pilot is home.
    ship: Option<ShipId>,
    events_tx: mpsc::UnboundedSender<PilotEvent>,
}

impl Pilot<'_> {
    /// Passes the pilot's datagrams and control messages to the expedition,
    /// and the expedition's news to the pilot, until the session ends.
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
                    Ok(bytes) => match (InputDatagram::decode(&bytes), self.ship) {
                        (Ok(input), Some(ship)) => self.expedition.input(ship, input),
                        // Inputs sent while at home, after destruction, go nowhere.
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
                    PilotEvent::Destroyed { lost } => {
                        self.ship = None;
                        let message = ServerMessage::Destroyed {
                            modules_lost: lost.upgraded_modules,
                            cargo_lost: lost.cargo,
                        };
                        write_frame(&mut self.send, &message).await?;
                    }
                },
                _ = self.session.closed() => return Ok(()),
            }
        }
    }

    async fn handle(&mut self, message: ClientMessage) -> Result<(), SessionError> {
        match (message, self.ship) {
            (ClientMessage::Lock { target }, Some(ship)) => {
                self.expedition.lock(ship, target.map(ShipId));
            }
            // A lock sent just before destruction arrives too late to matter.
            (ClientMessage::Lock { .. }, None) => {}
            (ClientMessage::Launch, None) => {
                let rejoined = self
                    .expedition
                    .join(self.session.clone(), self.events_tx.clone())
                    .await;
                match rejoined {
                    Some(ship) => {
                        self.ship = Some(ship);
                        write_frame(&mut self.send, &joined(self.expedition, ship)).await?;
                    }
                    // Someone took the place while this pilot was home.
                    None => write_frame(&mut self.send, &ServerMessage::ExpeditionFull).await?,
                }
            }
            (ClientMessage::Launch, Some(_)) | (ClientMessage::Hello { .. }, _) => {
                return Err(SessionError::Unexpected);
            }
        }
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
