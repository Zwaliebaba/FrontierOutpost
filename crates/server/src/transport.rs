//! The WebTransport endpoint: accepts sessions and runs each one.
//!
//! A session opens with one bidirectional stream, the control stream, which
//! carries framed JSON messages ([`frontier_protocol::frame`]). The first
//! client message must be `Hello`; the server answers with [`handshake`].

use std::net::SocketAddr;

use frontier_protocol::input::InputDatagram;
use frontier_protocol::{ClientMessage, ServerMessage, frame};
use frontier_sim::TICKS_PER_SECOND;
use tracing::{debug, info, warn};
use web_transport_quinn::{RecvStream, SendStream, Server, ServerBuilder, Session};

use crate::cert::DevCertificate;
use crate::expedition::Expedition;
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
}

/// Session close code for a client that broke the protocol.
const PROTOCOL_VIOLATION: u32 = 1;
/// Session close code for a client whose protocol version is not supported.
const VERSION_REJECTED: u32 = 2;
/// Session close code when the expedition has no room.
const EXPEDITION_FULL: u32 = 3;

/// Binds the WebTransport endpoint.
///
/// # Errors
/// Fails if the address cannot be bound or the certificate is unusable.
pub fn bind(
    addr: SocketAddr,
    cert: &DevCertificate,
) -> Result<Server, web_transport_quinn::ServerError> {
    ServerBuilder::new()
        .with_addr(addr)
        .with_certificate(cert.chain.clone(), cert.key.clone_key())
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

    let Some(ship) = expedition.join(session.clone()).await else {
        write_frame(&mut send, &ServerMessage::ExpeditionFull).await?;
        let _ = send.finish();
        session.close(EXPEDITION_FULL, b"expedition full");
        return Ok(());
    };
    let joined = ServerMessage::Joined {
        ship_id: ship.0,
        tick_rate: TICKS_PER_SECOND,
    };
    let result = match write_frame(&mut send, &joined).await {
        Ok(()) => receive_inputs(session, expedition, ship).await,
        Err(error) => Err(error),
    };
    expedition.leave(ship);
    result
}

/// Hands every input datagram to the expedition until the session ends.
async fn receive_inputs(
    session: &Session,
    expedition: &Expedition,
    ship: frontier_sim::world::ShipId,
) -> Result<(), SessionError> {
    loop {
        tokio::select! {
            datagram = session.read_datagram() => match datagram {
                Ok(bytes) => match InputDatagram::decode(&bytes) {
                    Ok(input) => expedition.input(ship, input),
                    // A malformed datagram is dropped, as a lost one would be.
                    Err(error) => debug!(%error, "datagram ignored"),
                },
                // The session closed while reading: the client left.
                Err(_) => return Ok(()),
            },
            _ = session.closed() => return Ok(()),
        }
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
