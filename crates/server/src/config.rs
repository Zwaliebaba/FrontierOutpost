//! Server settings, read from environment variables with local defaults.

use std::net::SocketAddr;
use std::path::PathBuf;
use std::time::Duration;

use crate::relay::Impairment;

/// Where the server listens and what it serves.
#[derive(Debug, Clone, PartialEq)]
pub struct Config {
    /// Plain HTTP for the client files and connection details.
    pub http_addr: SocketAddr,
    /// UDP for WebTransport.
    pub web_transport_addr: SocketAddr,
    /// The WebTransport address browsers are told to dial, when it differs from
    /// the one the server binds: in a container the server binds `0.0.0.0`,
    /// which a browser cannot dial, and the host publishes the port elsewhere.
    pub public_web_transport_addr: Option<SocketAddr>,
    /// The built client, normally `client/dist`.
    pub client_dir: PathBuf,
    /// Delay and loss to add for trying the game under latency. Inactive unless
    /// one of the `FRONTIER_IMPAIR_*` variables is set.
    pub impairment: Impairment,
}

/// Why the configuration could not be read.
#[derive(Debug, thiserror::Error)]
pub enum ConfigError {
    #[error("{name} is not {expected}: {value:?}")]
    Invalid {
        name: &'static str,
        expected: &'static str,
        value: String,
    },
    /// The relay listens on a port of its own choosing, so a fixed public
    /// address would send browsers past it to the unimpaired endpoint.
    #[error("{PUBLIC_WEB_TRANSPORT_ADDR} cannot be combined with the FRONTIER_IMPAIR_* variables")]
    PublicAddressWithImpairment,
}

const HTTP_ADDR: &str = "FRONTIER_HTTP_ADDR";
const WEB_TRANSPORT_ADDR: &str = "FRONTIER_WEBTRANSPORT_ADDR";
const PUBLIC_WEB_TRANSPORT_ADDR: &str = "FRONTIER_PUBLIC_WEBTRANSPORT_ADDR";
const CLIENT_DIR: &str = "FRONTIER_CLIENT_DIR";
const IMPAIR_DELAY_MS: &str = "FRONTIER_IMPAIR_DELAY_MS";
const IMPAIR_JITTER_MS: &str = "FRONTIER_IMPAIR_JITTER_MS";
const IMPAIR_LOSS_PERCENT: &str = "FRONTIER_IMPAIR_LOSS_PERCENT";

impl Config {
    /// Reads the configuration through `lookup`, which returns a variable's
    /// value if it is set. Loopback by default: a browser treats
    /// `http://127.0.0.1` as a secure context, which WebTransport requires.
    ///
    /// # Errors
    /// Fails if a variable is set but cannot be parsed, or if a public address
    /// is set together with an impairment.
    pub fn from_lookup(lookup: impl Fn(&str) -> Option<String>) -> Result<Self, ConfigError> {
        let parse_addr = |name: &'static str, value: String| {
            value.parse().map_err(|_| ConfigError::Invalid {
                name,
                expected: "a socket address",
                value,
            })
        };
        let addr = |name: &'static str, default: &str| {
            parse_addr(name, lookup(name).unwrap_or_else(|| default.to_owned()))
        };
        let number = |name: &'static str, max: f64| match lookup(name) {
            None => Ok(0.0),
            Some(value) => value
                .parse::<f64>()
                .ok()
                .filter(|number| (0.0..=max).contains(number))
                .ok_or(ConfigError::Invalid {
                    name,
                    expected: "a number in range",
                    value,
                }),
        };
        let config = Self {
            http_addr: addr(HTTP_ADDR, "127.0.0.1:8080")?,
            web_transport_addr: addr(WEB_TRANSPORT_ADDR, "127.0.0.1:4443")?,
            public_web_transport_addr: lookup(PUBLIC_WEB_TRANSPORT_ADDR)
                .map(|value| parse_addr(PUBLIC_WEB_TRANSPORT_ADDR, value))
                .transpose()?,
            client_dir: lookup(CLIENT_DIR)
                .map_or_else(|| PathBuf::from("client/dist"), PathBuf::from),
            impairment: Impairment {
                delay: Duration::from_secs_f64(number(IMPAIR_DELAY_MS, 10_000.0)? / 1_000.0),
                jitter: Duration::from_secs_f64(number(IMPAIR_JITTER_MS, 10_000.0)? / 1_000.0),
                loss: number(IMPAIR_LOSS_PERCENT, 100.0)? / 100.0,
            },
        };
        if config.public_web_transport_addr.is_some() && config.impairment.is_active() {
            return Err(ConfigError::PublicAddressWithImpairment);
        }
        Ok(config)
    }

    /// The WebTransport address to give browsers, given the address the server
    /// (or the relay in front of it) actually listens on.
    #[must_use]
    pub fn browser_addr(&self, listening: SocketAddr) -> SocketAddr {
        self.public_web_transport_addr.unwrap_or(listening)
    }

    /// Reads the configuration from the process environment.
    ///
    /// # Errors
    /// See [`Config::from_lookup`].
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
        assert_eq!(config.public_web_transport_addr, None);
    }

    #[test]
    fn tells_browsers_the_listening_address_by_default() {
        let config = Config::from_lookup(|_| None).unwrap();
        let listening = "127.0.0.1:50123".parse().unwrap();
        assert_eq!(config.browser_addr(listening), listening);
    }

    #[test]
    fn tells_browsers_the_public_address_when_one_is_set() {
        let config = Config::from_lookup(|name| match name {
            WEB_TRANSPORT_ADDR => Some("0.0.0.0:4443".into()),
            PUBLIC_WEB_TRANSPORT_ADDR => Some("127.0.0.1:4443".into()),
            _ => None,
        })
        .unwrap();
        assert_eq!(
            config.browser_addr(config.web_transport_addr),
            "127.0.0.1:4443".parse().unwrap()
        );
    }

    #[test]
    fn refuses_a_public_address_with_an_impairment() {
        let error = Config::from_lookup(|name| match name {
            PUBLIC_WEB_TRANSPORT_ADDR => Some("127.0.0.1:4443".into()),
            IMPAIR_DELAY_MS => Some("75".into()),
            _ => None,
        })
        .unwrap_err();
        assert!(matches!(error, ConfigError::PublicAddressWithImpairment));
    }

    #[test]
    fn refuses_a_public_address_that_does_not_parse() {
        let error =
            Config::from_lookup(|name| (name == PUBLIC_WEB_TRANSPORT_ADDR).then(|| "nope".into()))
                .unwrap_err();
        assert!(error.to_string().contains(PUBLIC_WEB_TRANSPORT_ADDR));
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
    fn reads_the_impairment_as_one_way_milliseconds_and_percent() {
        let config = Config::from_lookup(|name| match name {
            IMPAIR_DELAY_MS => Some("75".into()),
            IMPAIR_JITTER_MS => Some("10".into()),
            IMPAIR_LOSS_PERCENT => Some("2.5".into()),
            _ => None,
        })
        .unwrap();
        assert_eq!(config.impairment.delay, Duration::from_millis(75));
        assert_eq!(config.impairment.jitter, Duration::from_millis(10));
        assert!((config.impairment.loss - 0.025).abs() < 1e-12);
        assert!(
            !Config::from_lookup(|_| None)
                .unwrap()
                .impairment
                .is_active()
        );
    }

    #[test]
    fn refuses_an_impairment_out_of_range() {
        for (name, value) in [
            (IMPAIR_LOSS_PERCENT, "101"),
            (IMPAIR_DELAY_MS, "-5"),
            (IMPAIR_JITTER_MS, "lots"),
        ] {
            let error = Config::from_lookup(|asked| (asked == name).then(|| value.into()));
            assert!(error.is_err(), "{name}={value} should be refused");
        }
    }

    #[test]
    fn names_the_variable_that_is_wrong() {
        let error = Config::from_lookup(|name| (name == WEB_TRANSPORT_ADDR).then(|| "nope".into()))
            .unwrap_err();
        assert!(error.to_string().contains(WEB_TRANSPORT_ADDR));
    }
}
