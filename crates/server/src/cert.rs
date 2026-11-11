//! The WebTransport server's certificate.
//!
//! Locally, a self-signed certificate is generated when the server starts.
//! Browsers accept one for WebTransport only if they are given its SHA-256 hash
//! and it is valid for at most 14 days, so it cannot be a file kept in the
//! repository. A server reachable under a real domain name instead uses a
//! certificate from an authority, read from files that something else (Caddy,
//! on the Azure host) obtains and renews.

use std::path::{Path, PathBuf};
use std::time::SystemTime;

use rustls::pki_types::pem::PemObject;
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

/// A certificate chain and its key, in the PEM files an authority's client
/// writes: `FRONTIER_TLS_CERT` and `FRONTIER_TLS_KEY`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CertificateFiles {
    pub chain: PathBuf,
    pub key: PathBuf,
}

/// A certificate chain and key read from [`CertificateFiles`].
#[derive(Debug)]
pub struct LoadedCertificate {
    pub chain: Vec<CertificateDer<'static>>,
    pub key: PrivateKeyDer<'static>,
}

#[derive(Debug, thiserror::Error)]
pub enum LoadError {
    #[error("{path}: {error}")]
    Pem {
        path: PathBuf,
        error: rustls::pki_types::pem::Error,
    },
    #[error("{0}: no certificate in the file")]
    NoCertificate(PathBuf),
}

impl CertificateFiles {
    /// Reads the chain and key. The key may be PKCS#8, PKCS#1 or SEC1 (the
    /// `EC PRIVATE KEY` that Caddy writes).
    ///
    /// # Errors
    /// Fails if a file is missing or holds no certificate or key.
    pub fn load(&self) -> Result<LoadedCertificate, LoadError> {
        fn pem(path: &Path) -> impl FnOnce(rustls::pki_types::pem::Error) -> LoadError + '_ {
            move |error| LoadError::Pem {
                path: path.to_owned(),
                error,
            }
        }
        let chain = CertificateDer::pem_file_iter(&self.chain)
            .map_err(pem(&self.chain))?
            .collect::<Result<Vec<_>, _>>()
            .map_err(pem(&self.chain))?;
        if chain.is_empty() {
            return Err(LoadError::NoCertificate(self.chain.clone()));
        }
        let key = PrivateKeyDer::from_pem_file(&self.key).map_err(pem(&self.key))?;
        Ok(LoadedCertificate { chain, key })
    }

    /// When either file last changed, if both exist: a renewal shows as a
    /// later time.
    #[must_use]
    pub fn modified(&self) -> Option<SystemTime> {
        let chain = std::fs::metadata(&self.chain).ok()?.modified().ok()?;
        let key = std::fs::metadata(&self.key).ok()?.modified().ok()?;
        Some(chain.max(key))
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

    /// A certificate for `127.0.0.1` signed by its own new authority, written
    /// as PEM files into `dir`.
    fn issue(dir: &Path) -> CertificateFiles {
        let ca_key = rcgen::KeyPair::generate().unwrap();
        let mut ca = rcgen::CertificateParams::new(Vec::<String>::new()).unwrap();
        ca.is_ca = rcgen::IsCa::Ca(rcgen::BasicConstraints::Unconstrained);
        let ca = ca.self_signed(&ca_key).unwrap();
        let key = rcgen::KeyPair::generate().unwrap();
        let leaf = rcgen::CertificateParams::new(vec!["127.0.0.1".to_owned()])
            .unwrap()
            .signed_by(
                &key,
                &rcgen::Issuer::new(rcgen::CertificateParams::default(), &ca_key),
            )
            .unwrap();
        let files = CertificateFiles {
            chain: dir.join("cert.pem"),
            key: dir.join("key.pem"),
        };
        std::fs::write(&files.chain, leaf.pem() + &ca.pem()).unwrap();
        std::fs::write(&files.key, key.serialize_pem()).unwrap();
        files
    }

    fn scratch(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("frontier-cert-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn loads_a_chain_and_its_key_from_pem_files() {
        let dir = scratch("load");
        let files = issue(&dir);
        let loaded = files.load().unwrap();
        assert_eq!(loaded.chain.len(), 2, "the leaf and its issuer");
        assert!(matches!(loaded.key, PrivateKeyDer::Pkcs8(_)));
        assert!(files.modified().is_some());
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn refuses_missing_or_empty_files() {
        let dir = scratch("missing");
        let files = CertificateFiles {
            chain: dir.join("none.pem"),
            key: dir.join("none.key"),
        };
        assert!(files.load().is_err());
        assert_eq!(files.modified(), None);
        std::fs::write(&files.chain, "").unwrap();
        std::fs::write(&files.key, "").unwrap();
        assert!(matches!(files.load(), Err(LoadError::NoCertificate(_))));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
