//! Shared setup for tests that talk to a real server over WebTransport.
#![allow(
    dead_code,
    clippy::unwrap_used,
    reason = "each test binary uses a different subset; helpers panic to fail the test"
)]

use std::sync::Arc;

use frontier_protocol::{ClientMessage, PROTOCOL_VERSION, ServerMessage, frame};
use frontier_server::auth::Auth;
use frontier_server::cert::DevCertificate;
use frontier_server::store::Store;
use frontier_server::transport;
use frontier_server::transport::{Identity, Settings};
use rustls::client::danger::{HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier};
use rustls::crypto::{CryptoProvider, verify_tls13_signature};
use rustls::pki_types::{CertificateDer, ServerName, UnixTime};
use rustls::{DigitallySignedStruct, SignatureScheme};
use time::OffsetDateTime;
use web_transport_quinn::proto::ConnectRequest;
use web_transport_quinn::quinn;
use web_transport_quinn::{Client, RecvStream, SendStream, Session, crypto};
pub use webauthn_authenticator_rs::WebauthnAuthenticator;
pub use webauthn_authenticator_rs::softpasskey::SoftPasskey;

/// What tests launch: the compact site with a fixed seed.
pub fn test_settings() -> Settings {
    Settings {
        template: frontier_sim::site::COMPACT_SITE,
        open_tiers: 1,
        seed: Some(7),
        seed_pool: transport::SeedPool::default(),
        opening_threat: None,
    }
}

/// Starts a server on an ephemeral loopback port and returns its URL and the
/// hash of its certificate.
pub fn start_server() -> (url::Url, [u8; 32]) {
    start_server_with(test_settings(), transport::IDLE_TIMEOUT)
}

/// As [`start_server`], dropping sessions that go silent for `idle_timeout`.
pub fn start_server_with_idle_timeout(idle_timeout: std::time::Duration) -> (url::Url, [u8; 32]) {
    start_server_with(test_settings(), idle_timeout)
}

/// As [`start_server`], and returns the world's store, for tests that set a
/// pilot's holdings directly.
pub fn start_server_with_store() -> (url::Url, [u8; 32], Store) {
    let store = Store::in_memory().unwrap();
    let (url, sha256) = serve_store(test_settings(), transport::IDLE_TIMEOUT, store.clone());
    (url, sha256, store)
}

/// As [`start_server`], with these settings and idle timeout.
pub fn start_server_with(
    settings: Settings,
    idle_timeout: std::time::Duration,
) -> (url::Url, [u8; 32]) {
    serve_store(settings, idle_timeout, Store::in_memory().unwrap())
}

fn serve_store(
    settings: Settings,
    idle_timeout: std::time::Duration,
    store: Store,
) -> (url::Url, [u8; 32]) {
    let cert =
        DevCertificate::generate(&["localhost", "127.0.0.1"], OffsetDateTime::now_utc()).unwrap();
    let server =
        transport::bind_with_idle_timeout("127.0.0.1:0".parse().unwrap(), &cert, idle_timeout)
            .unwrap();
    let addr = server.local_addr().unwrap();
    let identity = Identity {
        store,
        auth: Auth::new(&origin()).unwrap(),
        runs: frontier_server::runs::Runs::default(),
        crews: frontier_server::crews::Crews::default(),
    };
    tokio::spawn(transport::serve(server, settings, identity));
    (format!("https://{addr}/").parse().unwrap(), cert.sha256)
}

/// The origin test servers bind passkeys to: where a browser would load the game.
pub fn origin() -> url::Url {
    "http://localhost:8080".parse().unwrap()
}

/// Registers a new pilot with a software passkey after the handshake, and
/// returns their callsign and session token. The server's contract follows.
pub async fn sign_up(send: &mut SendStream, recv: &mut RecvStream) -> (String, String) {
    sign_up_with(send, recv, &mut SoftPasskey::new(true)).await
}

/// As [`sign_up`], registering the passkey on `key`.
pub async fn sign_up_with(
    send: &mut SendStream,
    recv: &mut RecvStream,
    key: &mut SoftPasskey,
) -> (String, String) {
    use std::sync::atomic::{AtomicU32, Ordering};
    static NEXT: AtomicU32 = AtomicU32::new(0);
    let callsign = format!("Pilot-{}", NEXT.fetch_add(1, Ordering::Relaxed));
    write(
        send,
        &ClientMessage::Register {
            callsign: callsign.clone(),
        },
    )
    .await;
    let ServerMessage::PasskeyOptions { options } = read(recv).await else {
        panic!("expected passkey options");
    };
    let credential = key
        .do_registration(origin(), serde_json::from_str(&options).unwrap())
        .unwrap();
    write(
        send,
        &ClientMessage::PasskeyCredential {
            credential: serde_json::to_string(&credential).unwrap(),
        },
    )
    .await;
    let ServerMessage::SignedIn { token, account, .. } = read(recv).await else {
        panic!("expected SignedIn");
    };
    assert_eq!(account.credits, 100);
    (callsign, token)
}

/// Connects to a fresh server the way a browser would: by pinning the
/// certificate's hash.
pub async fn connect() -> Session {
    let (url, sha256) = start_server();
    pinned_client(sha256)
        .connect(ConnectRequest::new(url))
        .await
        .unwrap()
}

/// A client that accepts only the certificate with this hash.
///
/// `web_transport_quinn::ClientBuilder` would do this, but it binds its socket
/// to the IPv6 wildcard and panics on machines without IPv6, so the endpoint is
/// built here on IPv4 loopback instead. Browsers are not affected.
pub fn pinned_client(sha256: [u8; 32]) -> Client {
    let provider = crypto::default_provider();
    let mut tls = rustls::ClientConfig::builder_with_provider(provider.clone())
        .with_protocol_versions(&[&rustls::version::TLS13])
        .unwrap()
        .dangerous()
        .with_custom_certificate_verifier(Arc::new(Pinned { sha256, provider }))
        .with_no_client_auth();
    tls.alpn_protocols = vec![web_transport_quinn::ALPN.as_bytes().to_vec()];

    let quic = quinn::crypto::rustls::QuicClientConfig::try_from(tls).unwrap();
    let endpoint = quinn::Endpoint::client("127.0.0.1:0".parse().unwrap()).unwrap();
    Client::new(endpoint, quinn::ClientConfig::new(Arc::new(quic)))
}

/// Accepts the server's certificate if its SHA-256 matches, as a browser does
/// with `serverCertificateHashes`, and still checks the handshake signature.
#[derive(Debug)]
pub struct Pinned {
    sha256: [u8; 32],
    provider: Arc<CryptoProvider>,
}

impl ServerCertVerifier for Pinned {
    fn verify_server_cert(
        &self,
        end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _server_name: &ServerName<'_>,
        _ocsp_response: &[u8],
        _now: UnixTime,
    ) -> Result<ServerCertVerified, rustls::Error> {
        if crypto::sha256(&self.provider, end_entity).as_ref() == self.sha256 {
            Ok(ServerCertVerified::assertion())
        } else {
            Err(rustls::Error::General(
                "certificate hash does not match".into(),
            ))
        }
    }

    fn verify_tls12_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        Err(rustls::Error::General("QUIC does not use TLS 1.2".into()))
    }

    fn verify_tls13_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        verify_tls13_signature(
            message,
            cert,
            dss,
            &self.provider.signature_verification_algorithms,
        )
    }

    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> {
        self.provider
            .signature_verification_algorithms
            .supported_schemes()
    }
}

/// A client that trusts certificates issued by `root`, as a browser trusts a
/// public authority: no hash. On IPv4 loopback, as [`pinned_client`].
pub fn trusting_client(root: CertificateDer<'static>) -> Client {
    let mut roots = rustls::RootCertStore::empty();
    roots.add(root).unwrap();
    let mut tls = rustls::ClientConfig::builder_with_provider(crypto::default_provider())
        .with_protocol_versions(&[&rustls::version::TLS13])
        .unwrap()
        .with_root_certificates(roots)
        .with_no_client_auth();
    tls.alpn_protocols = vec![web_transport_quinn::ALPN.as_bytes().to_vec()];
    let quic = quinn::crypto::rustls::QuicClientConfig::try_from(tls).unwrap();
    let endpoint = quinn::Endpoint::client("127.0.0.1:0".parse().unwrap()).unwrap();
    Client::new(endpoint, quinn::ClientConfig::new(Arc::new(quic)))
}

/// A new authority, and a certificate it issued for `names`, written as the
/// PEM files `cert.pem` (the chain) and `key.pem` into `dir`. Returns the files
/// and the authority's own certificate, for a client to trust.
pub fn issue_certificate(
    dir: &std::path::Path,
    names: &[&str],
) -> (
    frontier_server::cert::CertificateFiles,
    CertificateDer<'static>,
) {
    let ca_key = rcgen::KeyPair::generate().unwrap();
    let mut params = rcgen::CertificateParams::new(Vec::<String>::new()).unwrap();
    params.is_ca = rcgen::IsCa::Ca(rcgen::BasicConstraints::Unconstrained);
    params
        .distinguished_name
        .push(rcgen::DnType::CommonName, "Test authority");
    let ca = params.clone().self_signed(&ca_key).unwrap();
    let key = rcgen::KeyPair::generate().unwrap();
    let names: Vec<String> = names.iter().map(|name| (*name).to_owned()).collect();
    let leaf = rcgen::CertificateParams::new(names)
        .unwrap()
        .signed_by(&key, &rcgen::Issuer::new(params, &ca_key))
        .unwrap();
    let files = frontier_server::cert::CertificateFiles {
        chain: dir.join("cert.pem"),
        key: dir.join("key.pem"),
    };
    std::fs::write(&files.chain, leaf.pem() + &ca.pem()).unwrap();
    std::fs::write(&files.key, key.serialize_pem()).unwrap();
    (files, ca.der().clone())
}

/// Connects to a server and returns the session with its control stream.
pub async fn connect_to(url: &url::Url, sha256: [u8; 32]) -> (Session, SendStream, RecvStream) {
    let session = pinned_client(sha256)
        .connect(ConnectRequest::new(url.clone()))
        .await
        .unwrap();
    let (send, recv) = session.open_bi().await.unwrap();
    (session, send, recv)
}

pub async fn write(send: &mut SendStream, message: &ClientMessage) {
    send.write_all(&frame::encode(message).unwrap())
        .await
        .unwrap();
}

pub async fn read(recv: &mut RecvStream) -> ServerMessage {
    // The catalog follows every sign-in and never changes; tests that care
    // about it read it with `read_any`.
    loop {
        let message = read_any(recv).await;
        if !matches!(message, ServerMessage::Catalog { .. }) {
            return message;
        }
    }
}

/// Reads the next message, whatever it is.
pub async fn read_any(recv: &mut RecvStream) -> ServerMessage {
    let mut header = [0; frame::HEADER_BYTES];
    recv.read_exact(&mut header).await.unwrap();
    let mut body = vec![0; frame::body_length(header).unwrap()];
    recv.read_exact(&mut body).await.unwrap();
    frame::decode(&body).unwrap()
}

/// Sends Hello and returns the server's answer.
pub async fn hello(send: &mut SendStream, recv: &mut RecvStream) -> ServerMessage {
    write(
        send,
        &ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION,
        },
    )
    .await;
    read(recv).await
}
