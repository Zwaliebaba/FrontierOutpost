//! WebTransport spike server.
//!
//! Answers one question before the game commits to a transport: can a browser exchange
//! streams and datagrams with a Rust server built on quinn and `web-transport-quinn`, using
//! nothing but a locally generated certificate?
//!
//! Run it with `cargo run`, then open the printed address. This is throwaway code and is not
//! part of the game's workspace.

use std::net::{Ipv4Addr, SocketAddr};
use std::sync::Arc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use anyhow::Context;
use bytes::Bytes;
use rustls::pki_types::{CertificateDer, PrivateKeyDer, PrivatePkcs8KeyDer};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, UdpSocket};
use web_transport_quinn::{RecvStream, SendStream, ServerBuilder, Session, crypto};

const PAGE: &str = include_str!("../index.html");
const QUIC_PORT: u16 = 4443;
const HTTP_PORT: u16 = 4444;
/// A second way in, through a relay that delays and drops packets like a poor connection.
const RELAY_PORT: u16 = 4445;
const RELAY_DELAY_MS: u64 = 40;
const RELAY_LOSS_PERCENT: u64 = 3;
/// Browsers only accept a pinned certificate hash if the certificate is valid for at most
/// 14 days, so a fresh one is generated on every start.
const CERT_DAYS: i64 = 13;
const MAX_REQUEST_BYTES: usize = 256 * 1024;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let (chain, key, hash) = certificate()?;

    let quic: SocketAddr = (Ipv4Addr::LOCALHOST, QUIC_PORT).into();
    let mut server = ServerBuilder::new()
        .with_addr(quic)
        .with_certificate(chain, key)
        .context("failed to start the WebTransport server")?;

    let relay = UdpSocket::bind((Ipv4Addr::LOCALHOST, RELAY_PORT))
        .await
        .context("failed to bind the relay")?;
    tokio::spawn(async move {
        if let Err(error) = run_relay(relay, quic).await {
            eprintln!("relay stopped: {error:#}");
        }
    });

    let page = PAGE
        .replace("__CERT_HASH__", &hash)
        .replace("__QUIC_PORT__", &QUIC_PORT.to_string())
        .replace("__RELAY_PORT__", &RELAY_PORT.to_string())
        .replace("__RELAY_DELAY_MS__", &RELAY_DELAY_MS.to_string())
        .replace("__RELAY_LOSS_PERCENT__", &RELAY_LOSS_PERCENT.to_string());
    let http = TcpListener::bind((Ipv4Addr::LOCALHOST, HTTP_PORT))
        .await
        .context("failed to bind the page server")?;
    tokio::spawn(serve_page(http, page));

    println!("WebTransport on https://127.0.0.1:{QUIC_PORT} (UDP)");
    println!("certificate sha-256: {hash}");
    println!("open http://127.0.0.1:{HTTP_PORT}/ in the browser under test");
    println!(
        "then http://127.0.0.1:{HTTP_PORT}/?impaired for {RELAY_DELAY_MS} ms each way and \
         {RELAY_LOSS_PERCENT}% loss"
    );

    while let Some(request) = server.accept().await {
        tokio::spawn(async move {
            match request.ok().await {
                Ok(session) => {
                    println!("session opened");
                    let reason = run_session(session).await;
                    println!("session closed: {reason:#}");
                }
                Err(error) => eprintln!("handshake failed: {error:#}"),
            }
        });
    }
    Ok(())
}

/// A self-signed ECDSA P-256 certificate for localhost, and the hex SHA-256 of its DER form.
fn certificate() -> anyhow::Result<(Vec<CertificateDer<'static>>, PrivateKeyDer<'static>, String)> {
    let mut params =
        rcgen::CertificateParams::new(vec!["localhost".to_owned(), "127.0.0.1".to_owned()])?;
    let now = time::OffsetDateTime::now_utc();
    params.not_before = now - time::Duration::hours(1);
    params.not_after = now + time::Duration::days(CERT_DAYS);

    let key = rcgen::KeyPair::generate()?;
    let cert = params.self_signed(&key)?.der().clone();

    let hash = crypto::sha256(&crypto::default_provider(), &cert);
    let hex = hash
        .as_ref()
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect();
    let key = PrivateKeyDer::Pkcs8(PrivatePkcs8KeyDer::from(key.serialize_der()));
    Ok((vec![cert], key, hex))
}

/// Serves the test page to every request. A loopback address counts as a secure context, so
/// the page may use WebTransport without being served over HTTPS.
async fn serve_page(listener: TcpListener, page: String) {
    loop {
        let Ok((mut socket, _)) = listener.accept().await else {
            continue;
        };
        let page = page.clone();
        tokio::spawn(async move {
            let mut request = [0_u8; 2048];
            let _ = socket.read(&mut request).await;
            let response = format!(
                "HTTP/1.1 200 OK\r\ncontent-type: text/html; charset=utf-8\r\n\
                 content-length: {}\r\ncache-control: no-store\r\nconnection: close\r\n\r\n{page}",
                page.len()
            );
            let _ = socket.write_all(response.as_bytes()).await;
        });
    }
}

/// Forwards UDP between one browser and the server, delaying every packet and dropping some.
/// QUIC sees it as a slow, lossy path, which loopback alone never is.
async fn run_relay(front: UdpSocket, server: SocketAddr) -> anyhow::Result<()> {
    let front = Arc::new(front);
    let back = Arc::new(UdpSocket::bind((Ipv4Addr::LOCALHOST, 0)).await?);
    back.connect(server).await?;
    let delay = Duration::from_millis(RELAY_DELAY_MS);

    let mut client: Option<SocketAddr> = None;
    let mut dice = Dice::new();
    let mut inbound = vec![0_u8; 65_536];
    let mut outbound = vec![0_u8; 65_536];
    loop {
        tokio::select! {
            received = front.recv_from(&mut inbound) => {
                let (length, from) = received?;
                client = Some(from);
                if dice.drops() {
                    continue;
                }
                let (packet, back) = (inbound[..length].to_vec(), back.clone());
                tokio::spawn(async move {
                    tokio::time::sleep(delay).await;
                    let _ = back.send(&packet).await;
                });
            }
            received = back.recv(&mut outbound) => {
                let length = received?;
                let Some(to) = client else { continue };
                if dice.drops() {
                    continue;
                }
                let (packet, front) = (outbound[..length].to_vec(), front.clone());
                tokio::spawn(async move {
                    tokio::time::sleep(delay).await;
                    let _ = front.send_to(&packet, to).await;
                });
            }
        }
    }
}

/// A small xorshift generator: enough to decide which packets to drop.
struct Dice(u64);

impl Dice {
    fn new() -> Self {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_or(1, |d| d.as_nanos());
        Self((nanos as u64) | 1)
    }

    fn drops(&mut self) -> bool {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0 % 100 < RELAY_LOSS_PERCENT
    }
}

/// Echoes datagrams and answers each bidirectional stream until the session ends.
async fn run_session(session: Session) -> anyhow::Error {
    loop {
        tokio::select! {
            stream = session.accept_bi() => match stream {
                Ok((send, recv)) => {
                    let session = session.clone();
                    tokio::spawn(async move {
                        if let Err(error) = answer(session, send, recv).await {
                            eprintln!("request failed: {error:#}");
                        }
                    });
                }
                Err(error) => return error.into(),
            },
            datagram = session.read_datagram() => match datagram {
                // A datagram may be dropped on the way back as well; that is the point of them.
                Ok(datagram) => drop(session.send_datagram(datagram)),
                Err(error) => return error.into(),
            },
        }
    }
}

/// A request is either `push <uni|one|dgram> <count> <bytes> <hz>` or a payload to echo.
async fn answer(
    session: Session,
    mut send: SendStream,
    mut recv: RecvStream,
) -> anyhow::Result<()> {
    let request = recv.read_to_end(MAX_REQUEST_BYTES).await?;
    let reply = match std::str::from_utf8(&request).ok().and_then(parse_push) {
        Some(push) => format!("sent {}", push.run(&session).await?).into_bytes(),
        None => request,
    };
    send.write_all(&reply).await?;
    send.finish()?;
    Ok(())
}

#[derive(Clone, Copy, PartialEq)]
enum Delivery {
    /// Unreliable: a lost datagram is gone.
    Datagram,
    /// Reliable, each message on a stream of its own: a loss delays only that message.
    StreamEach,
    /// Reliable and ordered on one stream, as a WebSocket would be: a loss delays everything
    /// behind it.
    OneStream,
}

struct Push {
    delivery: Delivery,
    count: u32,
    bytes: usize,
    hz: f64,
}

fn parse_push(text: &str) -> Option<Push> {
    let mut parts = text.split(' ');
    if parts.next()? != "push" {
        return None;
    }
    let delivery = match parts.next()? {
        "uni" => Delivery::StreamEach,
        "one" => Delivery::OneStream,
        "dgram" => Delivery::Datagram,
        _ => return None,
    };
    let count = parts.next()?.parse().ok().filter(|count| *count <= 2000)?;
    let bytes = parts
        .next()?
        .parse()
        .ok()
        .filter(|bytes| (4..=65_536).contains(bytes))?;
    let hz = parts
        .next()?
        .parse()
        .ok()
        .filter(|hz| (1.0..=120.0).contains(hz))?;
    Some(Push {
        delivery,
        count,
        bytes,
        hz,
    })
}

impl Push {
    /// Sends `count` messages at `hz`, the way the game would send snapshots. Returns how many
    /// the server managed to hand to QUIC.
    async fn run(&self, session: &Session) -> anyhow::Result<u32> {
        let mut ticker = tokio::time::interval(Duration::from_secs_f64(1.0 / self.hz));
        let mut sent = 0;
        let mut shared = match self.delivery {
            Delivery::OneStream => Some(session.open_uni().await?),
            _ => None,
        };
        for sequence in 0..self.count {
            ticker.tick().await;
            let mut payload = vec![b'.'; self.bytes];
            payload[..4].copy_from_slice(&sequence.to_be_bytes());
            match (self.delivery, shared.as_mut()) {
                (Delivery::Datagram, _) => {
                    if session.send_datagram(Bytes::from(payload)).is_ok() {
                        sent += 1;
                    }
                }
                (_, Some(stream)) => {
                    stream.write_all(&payload).await?;
                    sent += 1;
                }
                (_, None) => {
                    let mut stream = session.open_uni().await?;
                    stream.write_all(&payload).await?;
                    stream.finish()?;
                    sent += 1;
                }
            }
        }
        if let Some(mut stream) = shared {
            stream.finish()?;
        }
        Ok(sent)
    }
}
