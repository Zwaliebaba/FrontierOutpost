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

mod common;

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
        Self::start_on(&data_path(), env)
    }

    /// As [`Server::start`], keeping its saves at `data`.
    fn start_on(data: &std::path::Path, env: &[(&str, &str)]) -> Self {
        let mut child = Command::new(env!("CARGO_BIN_EXE_frontier-server"))
            .env("FRONTIER_HTTP_ADDR", "127.0.0.1:0")
            .env("FRONTIER_WEBTRANSPORT_ADDR", "127.0.0.1:0")
            .env("FRONTIER_DATA", data)
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
                // The "open" line names the page origin, which may be configured;
                // its http_addr field is where the server really listens.
                if line.contains(" open ")
                    && let Some((_, rest)) = line.split_once("http_addr=")
                {
                    let addr = rest
                        .split_whitespace()
                        .next()
                        .unwrap_or_default()
                        .to_owned();
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

/// The WebTransport URL and certificate hash a browser would use.
fn web_transport(server: &Server) -> (url::Url, [u8; 32]) {
    let info = server.connect_info();
    let hex = info.certificate_sha256.unwrap();
    let mut sha256 = [0_u8; 32];
    for (i, byte) in sha256.iter_mut().enumerate() {
        *byte = u8::from_str_radix(&hex[i * 2..i * 2 + 2], 16).unwrap();
    }
    (info.web_transport_url.parse().unwrap(), sha256)
}

/// Plan work item 4.4's proof: the server is killed mid-run and started again
/// on the same saves, and the pilot signs in and rejoins their run where its
/// last checkpoint left it.
#[tokio::test(flavor = "multi_thread")]
async fn a_run_survives_the_server_being_killed_and_restarted() {
    use common::{SoftPasskey, WebauthnAuthenticator, connect_to, hello, origin, read, write};
    use frontier_protocol::input::{Controls, InputDatagram};
    use frontier_protocol::snapshot::{RunPhase, Snapshot};
    use frontier_protocol::{ClientMessage, ServerMessage};

    let data = data_path();
    let env = [
        ("FRONTIER_SITE", "compact"),
        ("FRONTIER_SEED", "29"),
        // No opening threat: NPCs would block the way in, and whether they are
        // restored is the simulation's test (they are not).
        ("FRONTIER_NPCS", "0"),
        ("FRONTIER_ORIGIN", "http://localhost:8080"),
    ];
    let mut key = SoftPasskey::new(true);
    let server = Server::start_on(&data, &env);
    let (url, sha256) = web_transport(&server);
    let (session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    let (callsign, _) = common::sign_up_with(&mut send, &mut recv, &mut key).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Contract { .. }
    ));
    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Account { .. }
    ));
    let ServerMessage::Joined { ship_id, .. } = read(&mut recv).await else {
        panic!("expected Joined");
    };

    // Into the ore field on the cruise drive: entering it writes a checkpoint.
    let forward = Controls {
        thrust: 127,
        turn: 0,
        fire: false,
        cruise: true,
    };
    let mut entered = None;
    for sequence in 0..600_u16 {
        let input = InputDatagram {
            sequence,
            controls: vec![forward; 4],
        };
        let _ = session.send_datagram(input.encode().unwrap().into());
        let deadline = tokio::time::Instant::now() + Duration::from_millis(50);
        while let Ok(Ok(bytes)) = tokio::time::timeout_at(deadline, session.read_datagram()).await {
            let snapshot = Snapshot::decode(&bytes).unwrap();
            if snapshot.own.phase == RunPhase::Objective && entered.is_none() {
                entered = snapshot.entities.iter().find(|e| e.id == ship_id).copied();
            }
        }
        if entered.is_some() {
            break;
        }
    }
    let entered = entered.expect("the pilot reached the ore field");
    // Let the checkpoint reach the disk, then kill the server outright.
    tokio::time::sleep(Duration::from_millis(500)).await;
    drop(server);

    let server = Server::start_on(&data, &env);
    let (url, sha256) = web_transport(&server);
    let (session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    write(&mut send, &ClientMessage::SignIn { callsign }).await;
    let ServerMessage::PasskeyOptions { options } = read(&mut recv).await else {
        panic!("expected passkey options");
    };
    let credential = key
        .do_authentication(origin(), serde_json::from_str(&options).unwrap())
        .unwrap();
    write(
        &mut send,
        &ClientMessage::PasskeyCredential {
            credential: serde_json::to_string(&credential).unwrap(),
        },
    )
    .await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::SignedIn { .. }
    ));
    let ServerMessage::Joined {
        ship_id: rejoined, ..
    } = read(&mut recv).await
    else {
        panic!("the restored run takes the pilot back");
    };
    assert_eq!(rejoined, ship_id);

    let bytes = tokio::time::timeout(Duration::from_secs(2), session.read_datagram())
        .await
        .unwrap()
        .unwrap();
    let snapshot = Snapshot::decode(&bytes).unwrap();
    assert_eq!(snapshot.own.phase, RunPhase::Objective, "still in the site");
    let me = snapshot.entities.iter().find(|e| e.id == ship_id).unwrap();
    let moved = f32::from(me.x - entered.x).hypot(f32::from(me.z - entered.z)) / 32.0;
    assert!(
        moved < 30.0,
        "back near where the checkpoint was taken: {moved} m away"
    );
    let _ = std::fs::remove_file(&data);
}

/// A server asked to stop saves every run where it stands. This one is
/// stopped seconds after launch, in open space: before its first minute and
/// before any pocket, where the only other checkpoint is the one at launch.
#[cfg(unix)]
#[tokio::test(flavor = "multi_thread")]
async fn a_run_is_saved_where_it_stands_when_the_server_is_asked_to_stop() {
    use common::{SoftPasskey, WebauthnAuthenticator, connect_to, hello, origin, read, write};
    use frontier_protocol::input::{Controls, InputDatagram};
    use frontier_protocol::snapshot::Snapshot;
    use frontier_protocol::{ClientMessage, ServerMessage};

    let data = data_path();
    let env = [
        ("FRONTIER_SITE", "compact"),
        ("FRONTIER_SEED", "29"),
        ("FRONTIER_NPCS", "0"),
        ("FRONTIER_ORIGIN", "http://localhost:8080"),
    ];
    let mut key = SoftPasskey::new(true);
    let mut server = Server::start_on(&data, &env);
    let (url, sha256) = web_transport(&server);
    let (session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    let (callsign, _) = common::sign_up_with(&mut send, &mut recv, &mut key).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Contract { .. }
    ));
    write(&mut send, &ClientMessage::Launch { tier: 1 }).await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::Account { .. }
    ));
    let ServerMessage::Joined { ship_id, .. } = read(&mut recv).await else {
        panic!("expected Joined");
    };

    // Three seconds out from the dock on the main drive.
    let forward = Controls {
        thrust: 127,
        turn: 0,
        fire: false,
        cruise: false,
    };
    let (mut launched, mut last) = (None, None);
    for sequence in 0..60_u16 {
        let input = InputDatagram {
            sequence,
            controls: vec![forward; 4],
        };
        let _ = session.send_datagram(input.encode().unwrap().into());
        let deadline = tokio::time::Instant::now() + Duration::from_millis(50);
        while let Ok(Ok(bytes)) = tokio::time::timeout_at(deadline, session.read_datagram()).await {
            let snapshot = Snapshot::decode(&bytes).unwrap();
            last = snapshot.entities.iter().find(|e| e.id == ship_id).copied();
            launched = launched.or(last);
        }
    }
    let (launched, last) = (launched.unwrap(), last.unwrap());
    let metres = |a: &frontier_protocol::snapshot::EntityState,
                  b: &frontier_protocol::snapshot::EntityState| {
        f32::from(a.x - b.x).hypot(f32::from(a.z - b.z)) / 32.0
    };
    assert!(metres(&last, &launched) > 10.0, "well clear of the berth");

    let asked = Command::new("kill")
        .args(["-TERM", &server.child.id().to_string()])
        .status()
        .unwrap();
    assert!(asked.success());
    let status = tokio::task::spawn_blocking(move || server.child.wait())
        .await
        .unwrap()
        .unwrap();
    assert!(
        status.success(),
        "the server should exit normally: {status}"
    );

    let server = Server::start_on(&data, &env);
    let (url, sha256) = web_transport(&server);
    let (session, mut send, mut recv) = connect_to(&url, sha256).await;
    hello(&mut send, &mut recv).await;
    write(&mut send, &ClientMessage::SignIn { callsign }).await;
    let ServerMessage::PasskeyOptions { options } = read(&mut recv).await else {
        panic!("expected passkey options");
    };
    let credential = key
        .do_authentication(origin(), serde_json::from_str(&options).unwrap())
        .unwrap();
    write(
        &mut send,
        &ClientMessage::PasskeyCredential {
            credential: serde_json::to_string(&credential).unwrap(),
        },
    )
    .await;
    assert!(matches!(
        read(&mut recv).await,
        ServerMessage::SignedIn { .. }
    ));
    let ServerMessage::Joined {
        ship_id: rejoined, ..
    } = read(&mut recv).await
    else {
        panic!("the saved run takes the pilot back");
    };
    assert_eq!(rejoined, ship_id);
    let bytes = tokio::time::timeout(Duration::from_secs(2), session.read_datagram())
        .await
        .unwrap()
        .unwrap();
    let snapshot = Snapshot::decode(&bytes).unwrap();
    let me = snapshot.entities.iter().find(|e| e.id == ship_id).unwrap();
    // The ship coasted on for the moment between the last snapshot read and
    // the stop; the launch checkpoint would put it back at the berth.
    assert!(
        metres(me, &last) < metres(me, &launched),
        "restored where it stopped, {} m on, not at the berth {} m back",
        metres(me, &last),
        metres(me, &launched)
    );
    let _ = std::fs::remove_file(&data);
}

/// Plan work item 4.5: `frontier-server wipe` leaves an empty world.
#[test]
fn the_wipe_command_leaves_an_empty_world() {
    use frontier_server::store::Store;
    let data = data_path();
    let store = Store::open(&data).unwrap();
    let pilot = store.create_account("Vega").unwrap();
    store.issue_token(pilot).unwrap();
    let seat = frontier_server::store::Seat {
        account: pilot,
        ship: 0,
        share: 100,
    };
    store.save_checkpoint("run", &[seat], "{}").unwrap();
    let world = store.world().to_owned();
    drop(store);

    let status = Command::new(env!("CARGO_BIN_EXE_frontier-server"))
        .arg("wipe")
        .env("FRONTIER_DATA", &data)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .unwrap();
    assert!(status.success());

    let store = Store::open(&data).unwrap();
    assert!(
        store.is_empty().unwrap(),
        "no pilots, ledger, sessions or runs"
    );
    assert_ne!(store.world(), world, "a new world");
    let _ = std::fs::remove_file(&data);
}

#[test]
fn refuses_an_unknown_command() {
    let status = Command::new(env!("CARGO_BIN_EXE_frontier-server"))
        .arg("wiep")
        .env("FRONTIER_DATA", data_path())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .unwrap();
    assert!(!status.success());
}
