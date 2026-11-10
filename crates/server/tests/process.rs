//! The server binary as a container runs it: configured from the environment,
//! and stopped by a signal.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpStream;
use std::process::{Child, Command, Stdio};
use std::sync::mpsc;
use std::thread;
use std::time::Duration;

use frontier_protocol::{CONNECT_INFO_PATH, ConnectInfo};

/// A fresh save file for one server.
fn data_path() -> std::path::PathBuf {
    use std::sync::atomic::{AtomicU32, Ordering};
    static NEXT: AtomicU32 = AtomicU32::new(0);
    std::env::temp_dir().join(format!(
        "frontier-process-{}-{}.db",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ))
}

/// A running server binary, killed when dropped.
struct Server {
    child: Child,
    http_addr: String,
}

impl Server {
    /// Starts the binary on loopback ports of its own choosing, with `env` on
    /// top, and waits until it serves HTTP.
    fn start(env: &[(&str, &str)]) -> Self {
        let mut child = Command::new(env!("CARGO_BIN_EXE_frontier-server"))
            .env("FRONTIER_HTTP_ADDR", "127.0.0.1:0")
            .env("FRONTIER_WEBTRANSPORT_ADDR", "127.0.0.1:0")
            .env("FRONTIER_DATA", data_path())
            .env("NO_COLOR", "1")
            .envs(env.iter().copied())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();

        let stdout = child.stdout.take().unwrap();
        let (sender, receiver) = mpsc::channel();
        thread::spawn(move || {
            for line in BufReader::new(stdout).lines() {
                let Ok(line) = line else { return };
                if let Some((_, rest)) = line.split_once("open http://") {
                    let addr = rest.split('/').next().unwrap_or_default().to_owned();
                    let _ = sender.send(addr);
                }
            }
        });
        let http_addr = receiver
            .recv_timeout(Duration::from_secs(30))
            .expect("the server should report its HTTP address");
        Self { child, http_addr }
    }

    fn connect_info(&self) -> ConnectInfo {
        let mut stream = TcpStream::connect(&self.http_addr).unwrap();
        write!(
            stream,
            "GET {CONNECT_INFO_PATH} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n",
            self.http_addr
        )
        .unwrap();
        let mut response = String::new();
        stream.read_to_string(&mut response).unwrap();
        let (_, body) = response.split_once("\r\n\r\n").unwrap();
        serde_json::from_str(body).unwrap()
    }
}

impl Drop for Server {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

#[test]
fn tells_browsers_the_public_address_instead_of_the_bound_one() {
    let server = Server::start(&[("FRONTIER_PUBLIC_WEBTRANSPORT_ADDR", "127.0.0.1:4443")]);
    let info = server.connect_info();
    assert_eq!(info.web_transport_url, "https://127.0.0.1:4443/");
    assert!(info.certificate_sha256.is_some());
}

#[test]
fn refuses_to_start_with_a_public_address_and_an_impairment() {
    let status = Command::new(env!("CARGO_BIN_EXE_frontier-server"))
        .env("FRONTIER_HTTP_ADDR", "127.0.0.1:0")
        .env("FRONTIER_WEBTRANSPORT_ADDR", "127.0.0.1:0")
        .env("FRONTIER_PUBLIC_WEBTRANSPORT_ADDR", "127.0.0.1:4443")
        .env("FRONTIER_IMPAIR_DELAY_MS", "75")
        .env("FRONTIER_DATA", data_path())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .unwrap();
    assert!(!status.success());
}

/// `docker stop` sends SIGTERM. Unhandled, it kills an ordinary process and is
/// ignored by a container's PID 1; handled, the server exits normally.
#[cfg(unix)]
#[test]
fn stops_cleanly_on_sigterm() {
    let mut server = Server::start(&[]);
    let killed = Command::new("kill")
        .args(["-TERM", &server.child.id().to_string()])
        .status()
        .unwrap();
    assert!(killed.success());

    for _ in 0..100 {
        if let Some(status) = server.child.try_wait().unwrap() {
            assert!(
                status.success(),
                "the server should exit normally: {status}"
            );
            return;
        }
        thread::sleep(Duration::from_millis(100));
    }
    panic!("the server did not stop within 10 s of SIGTERM");
}
