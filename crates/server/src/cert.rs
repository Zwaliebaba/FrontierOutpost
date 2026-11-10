//! A self-signed certificate generated when the server starts.
//!
//! Browsers accept a self-signed certificate for WebTransport only if they are
//! given its SHA-256 hash and it is valid for at most 14 days, so it cannot be
//! a file kept in the repository. A server reachable under a real domain name
//! would use a certificate from an authority instead.

use rustls::pki_types::{CertificateDer, PrivateKeyDer, PrivatePkcs8KeyDer};
use time::{Duration, OffsetDateTime};
use web_transport_quinn::crypto;

/// Browsers refuse a pinned certificate valid for longer than 14 days.
pub const VALIDITY: Duration = Duration::days(13);

/// A certificate and key ready for the WebTransport server.
pub struct DevCertificate {
    pub chain: Vec<CertificateDer<'static>>,
    pub key: PrivateKeyDer<'static>,
    /// SHA-256 of the certificate's DER encoding.
    pub sha256: [u8; 32],
}

#[derive(Debug, thiserror::Error)]
#[error("could not generate the development certificate: {0}")]
pub struct CertificateError(#[from] rcgen::Error);

impl DevCertificate {
    /// Generates an ECDSA P-256 certificate for `names`, valid from an hour
    /// before `now` (to tolerate clock skew) until [`VALIDITY`] after it.
    ///
    /// # Errors
    /// Fails if rcgen cannot build the certificate.
    pub fn generate(names: &[&str], now: OffsetDateTime) -> Result<Self, CertificateError> {
        let names = names
            .iter()
            .map(|name| (*name).to_owned())
            .collect::<Vec<_>>();
        let mut params = rcgen::CertificateParams::new(names)?;
        params.not_before = now - Duration::hours(1);
        params.not_after = now + VALIDITY;

        let key = rcgen::KeyPair::generate()?;
        let cert = params.self_signed(&key)?.der().clone();
        let hash = crypto::sha256(&crypto::default_provider(), &cert);
        let mut sha256 = [0; 32];
        sha256.copy_from_slice(hash.as_ref());

        Ok(Self {
            chain: vec![cert],
            key: PrivateKeyDer::Pkcs8(PrivatePkcs8KeyDer::from(key.serialize_der())),
            sha256,
        })
    }

    /// The hash as lowercase hex, the form [`frontier_protocol::ConnectInfo`] carries.
    #[must_use]
    pub fn sha256_hex(&self) -> String {
        self.sha256
            .iter()
            .map(|byte| format!("{byte:02x}"))
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stays_inside_the_browsers_validity_limit() {
        assert!(VALIDITY + Duration::hours(1) <= Duration::days(14));
    }

    #[test]
    fn hashes_the_certificate_it_returns() {
        let cert = DevCertificate::generate(&["localhost"], OffsetDateTime::now_utc()).unwrap();
        let rehash = crypto::sha256(&crypto::default_provider(), &cert.chain[0]);
        assert_eq!(cert.sha256.as_slice(), rehash.as_ref());
        assert_eq!(cert.sha256_hex().len(), 64);
        assert!(
            cert.sha256_hex()
                .chars()
                .all(|c| c.is_ascii_hexdigit() && !c.is_ascii_uppercase())
        );
    }

    #[test]
    fn every_start_gets_a_new_certificate() {
        let now = OffsetDateTime::now_utc();
        let first = DevCertificate::generate(&["localhost"], now).unwrap();
        let second = DevCertificate::generate(&["localhost"], now).unwrap();
        assert_ne!(first.sha256, second.sha256);
    }
}
