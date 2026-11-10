//! A development relay that makes a fast local link behave like a slow, lossy
//! one, for trying the game under latency (plan work item 1.5).
//!
//! It forwards UDP between browsers and the WebTransport endpoint, delaying
//! every packet and dropping some. Each browser gets its own upstream socket,
//! so the server still sees one peer per client. Never enable it in play.

use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use tokio::net::UdpSocket;
use tokio::sync::Mutex;
use tracing::debug;

/// How the relay degrades the link. Applied separately in each direction.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Impairment {
    /// One-way delay added to every packet.
    pub delay: Duration,
    /// Extra one-way delay, chosen uniformly from zero to this per packet.
    /// Packets can be reordered, as on a real network.
    pub jitter: Duration,
    /// Chance of dropping a packet, from 0 to 1.
    pub loss: f64,
}

impl Impairment {
    /// Whether the relay would change anything.
    #[must_use]
    pub fn is_active(&self) -> bool {
        !self.delay.is_zero() || !self.jitter.is_zero() || self.loss > 0.0
    }
}

/// Binds the relay on `addr` and forwards to `upstream`. Returns the address
/// browsers should connect to.
///
/// # Errors
/// Fails if the socket cannot be bound.
pub async fn start(
    addr: SocketAddr,
    upstream: SocketAddr,
    impairment: Impairment,
) -> std::io::Result<SocketAddr> {
    let front = Arc::new(UdpSocket::bind(addr).await?);
    let local = front.local_addr()?;
    tokio::spawn(run(front, upstream, impairment));
    Ok(local)
}

async fn run(front: Arc<UdpSocket>, upstream: SocketAddr, impairment: Impairment) {
    let clients: Arc<Mutex<HashMap<SocketAddr, Arc<UdpSocket>>>> = Arc::default();
    let dice = Arc::new(Mutex::new(Dice::seeded()));
    let mut buffer = vec![0_u8; 65_536];
    loop {
        let Ok((length, client)) = front.recv_from(&mut buffer).await else {
            continue;
        };
        let packet = buffer[..length].to_vec();
        let back = {
            let mut clients = clients.lock().await;
            if let Some(socket) = clients.get(&client) {
                socket.clone()
            } else {
                match connect_upstream(upstream).await {
                    Ok(socket) => {
                        tokio::spawn(backward(
                            socket.clone(),
                            front.clone(),
                            client,
                            impairment,
                            dice.clone(),
                        ));
                        clients.insert(client, socket.clone());
                        socket
                    }
                    Err(error) => {
                        debug!(%error, "relay could not open an upstream socket");
                        continue;
                    }
                }
            }
        };
        if let Some(wait) = dice.lock().await.fate(&impairment) {
            tokio::spawn(async move {
                tokio::time::sleep(wait).await;
                let _ = back.send(&packet).await;
            });
        }
    }
}

async fn connect_upstream(upstream: SocketAddr) -> std::io::Result<Arc<UdpSocket>> {
    let any: SocketAddr = if upstream.is_ipv4() {
        "0.0.0.0:0".parse().expect("a literal IPv4 address parses")
    } else {
        "[::]:0".parse().expect("a literal IPv6 address parses")
    };
    let socket = UdpSocket::bind(any).await?;
    socket.connect(upstream).await?;
    Ok(Arc::new(socket))
}

/// Forwards packets from the server back to one client.
async fn backward(
    back: Arc<UdpSocket>,
    front: Arc<UdpSocket>,
    client: SocketAddr,
    impairment: Impairment,
    dice: Arc<Mutex<Dice>>,
) {
    let mut buffer = vec![0_u8; 65_536];
    while let Ok(length) = back.recv(&mut buffer).await {
        let packet = buffer[..length].to_vec();
        if let Some(wait) = dice.lock().await.fate(&impairment) {
            let front = front.clone();
            tokio::spawn(async move {
                tokio::time::sleep(wait).await;
                let _ = front.send_to(&packet, client).await;
            });
        }
    }
}

/// A small xorshift generator: enough to decide delays and drops.
struct Dice(u64);

impl Dice {
    fn seeded() -> Self {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_or(1, |elapsed| elapsed.subsec_nanos());
        Self(u64::from(nanos) | 1)
    }

    fn unit(&mut self) -> f64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        // The top 53 bits make an exact f64 in 0..1.
        (self.0 >> 11) as f64 / (1_u64 << 53) as f64
    }

    /// Returns how long to hold a packet, or `None` to drop it.
    fn fate(&mut self, impairment: &Impairment) -> Option<Duration> {
        if self.unit() < impairment.loss {
            return None;
        }
        Some(impairment.delay + impairment.jitter.mul_f64(self.unit()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_impairment_with_nothing_set_is_inactive() {
        let none = Impairment {
            delay: Duration::ZERO,
            jitter: Duration::ZERO,
            loss: 0.0,
        };
        assert!(!none.is_active());
        assert!(Impairment { loss: 0.01, ..none }.is_active());
    }

    #[test]
    fn delays_within_range_and_drops_about_the_loss_rate() {
        let impairment = Impairment {
            delay: Duration::from_millis(50),
            jitter: Duration::from_millis(20),
            loss: 0.1,
        };
        let mut dice = Dice(12_345);
        let fates: Vec<Option<Duration>> = (0..10_000).map(|_| dice.fate(&impairment)).collect();
        let dropped = fates.iter().filter(|fate| fate.is_none()).count();
        assert!((800..1_200).contains(&dropped), "{dropped} dropped");
        for wait in fates.into_iter().flatten() {
            assert!(wait >= Duration::from_millis(50) && wait <= Duration::from_millis(70));
        }
    }

    #[tokio::test]
    async fn forwards_both_ways_with_the_delay() {
        let echo = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let echo_addr = echo.local_addr().unwrap();
        tokio::spawn(async move {
            let mut buffer = [0; 64];
            while let Ok((length, from)) = echo.recv_from(&mut buffer).await {
                let _ = echo.send_to(&buffer[..length], from).await;
            }
        });
        let delay = Duration::from_millis(40);
        let impairment = Impairment {
            delay,
            jitter: Duration::ZERO,
            loss: 0.0,
        };
        let relay = start("127.0.0.1:0".parse().unwrap(), echo_addr, impairment)
            .await
            .unwrap();

        let client = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let started = tokio::time::Instant::now();
        client.send_to(b"ping", relay).await.unwrap();
        let mut buffer = [0; 64];
        let (length, from) = client.recv_from(&mut buffer).await.unwrap();
        assert_eq!(&buffer[..length], b"ping");
        assert_eq!(from, relay);
        assert!(started.elapsed() >= delay * 2);
    }
}
