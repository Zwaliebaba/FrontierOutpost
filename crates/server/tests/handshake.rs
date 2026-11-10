//! A real WebTransport client completes the opening handshake with the server.
#![allow(
    clippy::unwrap_used,
    reason = "test helpers fail the test by panicking"
)]

use std::sync::Arc;

use frontier_protocol::{ClientMessage, PROTOCOL_VERSION, ServerMessage, frame};
use frontier_server::cert::DevCertificate;
use frontier_server::transport;
use rustls::client::danger::{HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier};
use rustls::crypto::{CryptoProvider, verify_tls13_signature};
use rustls::pki_types::{CertificateDer, ServerName, UnixTime};
use rustls::{DigitallySignedStruct, SignatureScheme};
use time::OffsetDateTime;
use web_transport_quinn::proto::ConnectRequest;
use web_transport_quinn::quinn;
use web_transport_quinn::{Client, Session, crypto};

/// Starts a server on an ephemeral loopback port and returns its URL and the
/// hash of its certificate.
fn start_server() -> (url::Url, [u8; 32]) {
    let cert =
        DevCertificate::generate(&["localhost", "127.0.0.1"], OffsetDateTime::now_utc()).unwrap();
    let server = transport::bind("127.0.0.1:0".parse().unwrap(), &cert).unwrap();
    let addr = server.local_addr().unwrap();
    tokio::spawn(transport::serve(server));
    (format!("https://{addr}/").parse().unwrap(), cert.sha256)
}

/// Connects to a fresh server the way a browser would: by pinning the
/// certificate's hash.
async fn connect() -> Session {
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
fn pinned_client(sha256: [u8; 32]) -> Client {
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
struct Pinned {
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

#[tokio::test]
async fn refuses_a_server_whose_certificate_hash_does_not_match() {
    let (url, mut sha256) = start_server();
    sha256[0] ^= 0xff;
    let result = pinned_client(sha256)
        .connect(ConnectRequest::new(url))
        .await;
    assert!(result.is_err());
}

async fn exchange(session: &Session, hello: &ClientMessage) -> ServerMessage {
    let (mut send, mut recv) = session.open_bi().await.unwrap();
    send.write_all(&frame::encode(hello).unwrap())
        .await
        .unwrap();
    let mut header = [0; frame::HEADER_BYTES];
    recv.read_exact(&mut header).await.unwrap();
    let mut body = vec![0; frame::body_length(header).unwrap()];
    recv.read_exact(&mut body).await.unwrap();
    frame::decode(&body).unwrap()
}

#[tokio::test]
async fn welcomes_a_client_on_the_current_protocol() {
    let session = connect().await;
    let reply = exchange(
        &session,
        &ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION,
        },
    )
    .await;
    assert_eq!(
        reply,
        ServerMessage::Welcome {
            protocol_version: PROTOCOL_VERSION
        }
    );
}

#[tokio::test]
async fn rejects_another_protocol_and_closes_the_session() {
    let session = connect().await;
    let reply = exchange(
        &session,
        &ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION + 1,
        },
    )
    .await;
    assert!(matches!(reply, ServerMessage::Rejected { .. }));
    let closed = tokio::time::timeout(std::time::Duration::from_secs(5), session.closed()).await;
    assert!(closed.is_ok(), "the server should close a rejected session");
}

#[tokio::test]
async fn closes_a_session_that_does_not_open_with_hello() {
    let session = connect().await;
    let (mut send, _recv) = session.open_bi().await.unwrap();
    send.write_all(&[0, 0, 0, 2, b'{', b'}']).await.unwrap();
    let closed = tokio::time::timeout(std::time::Duration::from_secs(5), session.closed()).await;
    assert!(
        closed.is_ok(),
        "the server should close a session that breaks the protocol"
    );
}
