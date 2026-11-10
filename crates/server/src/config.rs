//! Server settings, read from environment variables with local defaults.

use std::net::SocketAddr;
use std::path::PathBuf;

/// Where the server listens and what it serves.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Config {
    /// Plain HTTP for the client files and connection details.
    pub http_addr: SocketAddr,
    /// UDP for WebTransport.
    pub web_transport_addr: SocketAddr,
    /// The built client, normally `client/dist`.
    pub client_dir: PathBuf,
}

/// Why the configuration could not be read.
#[derive(Debug, thiserror::Error)]
#[error("{name} is not a valid socket address: {value:?}")]
pub struct ConfigError {
    name: &'static str,
    value: String,
}

const HTTP_ADDR: &str = "FRONTIER_HTTP_ADDR";
const WEB_TRANSPORT_ADDR: &str = "FRONTIER_WEBTRANSPORT_ADDR";
const CLIENT_DIR: &str = "FRONTIER_CLIENT_DIR";

impl Config {
    /// Reads the configuration through `lookup`, which returns a variable's
    /// value if it is set. Loopback by default: a browser treats
    /// `http://127.0.0.1` as a secure context, which WebTransport requires.
    ///
    /// # Errors
    /// Fails if an address variable is set but cannot be parsed.
    pub fn from_lookup(lookup: impl Fn(&str) -> Option<String>) -> Result<Self, ConfigError> {
        let addr = |name: &'static str, default: &str| {
            let value = lookup(name).unwrap_or_else(|| default.to_owned());
            value.parse().map_err(|_| ConfigError { name, value })
        };
        Ok(Self {
            http_addr: addr(HTTP_ADDR, "127.0.0.1:8080")?,
            web_transport_addr: addr(WEB_TRANSPORT_ADDR, "127.0.0.1:4443")?,
            client_dir: lookup(CLIENT_DIR)
                .map_or_else(|| PathBuf::from("client/dist"), PathBuf::from),
        })
    }

    /// Reads the configuration from the process environment.
    ///
    /// # Errors
    /// Fails if an address variable is set but cannot be parsed.
    pub fn from_env() -> Result<Self, ConfigError> {
        Self::from_lookup(|name| std::env::var(name).ok())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_to_loopback() {
        let config = Config::from_lookup(|_| None).unwrap();
        assert!(config.http_addr.ip().is_loopback());
        assert!(config.web_transport_addr.ip().is_loopback());
        assert_eq!(config.client_dir, PathBuf::from("client/dist"));
    }

    #[test]
    fn reads_overrides() {
        let config = Config::from_lookup(|name| match name {
            HTTP_ADDR => Some("0.0.0.0:9000".into()),
            WEB_TRANSPORT_ADDR => Some("[::1]:5000".into()),
            CLIENT_DIR => Some("/srv/client".into()),
            _ => None,
        })
        .unwrap();
        assert_eq!(config.http_addr, "0.0.0.0:9000".parse().unwrap());
        assert_eq!(config.web_transport_addr, "[::1]:5000".parse().unwrap());
        assert_eq!(config.client_dir, PathBuf::from("/srv/client"));
    }

    #[test]
    fn names_the_variable_that_is_wrong() {
        let error = Config::from_lookup(|name| (name == WEB_TRANSPORT_ADDR).then(|| "nope".into()))
            .unwrap_err();
        assert!(error.to_string().contains(WEB_TRANSPORT_ADDR));
    }
}
