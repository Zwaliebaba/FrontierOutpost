//! Shared setup for tests that talk to a real server over WebTransport.
#![allow(
    dead_code,
    clippy::unwrap_used,
    reason = "each test binary uses a different subset; helpers panic to fail the test"
)]

use std::sync::Arc;

use frontier_protocol::{ClientMessage, PROTOCOL_VERSION, ServerMessage, frame};
use frontier_server::cert::DevCertificate;
use frontier_server::expedition::Expedition;
use frontier_server::transport;
use rustls::client::danger::{HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier};
use rustls::crypto::{CryptoProvider, verify_tls13_signature};
use rustls::pki_types::{CertificateDer, ServerName, UnixTime};
use rustls::{DigitallySignedStruct, SignatureScheme};
use time::OffsetDateTime;
use web_transport_quinn::proto::ConnectRequest;
use web_transport_quinn::quinn;
use web_transport_quinn::{Client, RecvStream, SendStream, Session, crypto};

/// Starts a server on an ephemeral loopback port and returns its URL and the
/// hash of its certificate.
pub fn start_server() -> (url::Url, [u8; 32]) {
    let cert =
        DevCertificate::generate(&["localhost", "127.0.0.1"], OffsetDateTime::now_utc()).unwrap();
    let server = transport::bind("127.0.0.1:0".parse().unwrap(), &cert).unwrap();
    let addr = server.local_addr().unwrap();
    tokio::spawn(transport::serve(server, Expedition::start()));
    (format!("https://{addr}/").parse().unwrap(), cert.sha256)
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
