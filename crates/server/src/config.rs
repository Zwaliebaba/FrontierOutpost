//! Server settings, read from environment variables with local defaults.

use std::net::SocketAddr;
use std::path::PathBuf;
use std::time::Duration;

use frontier_sim::site::{COMPACT_SITE, FRONTIER_SITE};
use frontier_sim::skirmish::MAX_NPCS;

use crate::relay::Impairment;
use crate::transport::Settings;

/// Where the server listens and what it serves.
#[derive(Debug, Clone, PartialEq)]
pub struct Config {
    /// Plain HTTP for the client files and connection details.
    pub http_addr: SocketAddr,
    /// UDP for WebTransport.
    pub web_transport_addr: SocketAddr,
    /// The built client, normally `client/dist`.
    pub client_dir: PathBuf,
    /// Delay and loss to add for trying the game under latency. Inactive unless
    /// one of the `FRONTIER_IMPAIR_*` variables is set.
    pub impairment: Impairment,
    /// What every launch offers and how.
    pub settings: Settings,
}

/// Why the configuration could not be read.
#[derive(Debug, thiserror::Error)]
#[error("{name} is not {expected}: {value:?}")]
pub struct ConfigError {
    name: &'static str,
    expected: &'static str,
    value: String,
}

const HTTP_ADDR: &str = "FRONTIER_HTTP_ADDR";
const WEB_TRANSPORT_ADDR: &str = "FRONTIER_WEBTRANSPORT_ADDR";
const CLIENT_DIR: &str = "FRONTIER_CLIENT_DIR";
const IMPAIR_DELAY_MS: &str = "FRONTIER_IMPAIR_DELAY_MS";
const IMPAIR_JITTER_MS: &str = "FRONTIER_IMPAIR_JITTER_MS";
const IMPAIR_LOSS_PERCENT: &str = "FRONTIER_IMPAIR_LOSS_PERCENT";
const SITE: &str = "FRONTIER_SITE";
const TIERS: &str = "FRONTIER_TIERS";
const SEED: &str = "FRONTIER_SEED";
const NPCS: &str = "FRONTIER_NPCS";

/// Reads a whole number from `min` to `max`, if the variable is set.
fn whole(
    lookup: &impl Fn(&str) -> Option<String>,
    name: &'static str,
    min: u32,
    max: u32,
) -> Result<Option<u32>, ConfigError> {
    lookup(name)
        .map(|value| {
            value
                .parse::<u32>()
                .ok()
                .filter(|number| (min..=max).contains(number))
                .ok_or(ConfigError {
                    name,
                    expected: "a whole number in range",
                    value,
                })
        })
        .transpose()
}

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
            value.parse().map_err(|_| ConfigError {
                name,
                expected: "a socket address",
                value,
            })
        };
        let number = |name: &'static str, max: f64| match lookup(name) {
            None => Ok(0.0),
            Some(value) => value
                .parse::<f64>()
                .ok()
                .filter(|number| (0.0..=max).contains(number))
                .ok_or(ConfigError {
                    name,
                    expected: "a number in range",
                    value,
                }),
        };
        Ok(Self {
            http_addr: addr(HTTP_ADDR, "127.0.0.1:8080")?,
            web_transport_addr: addr(WEB_TRANSPORT_ADDR, "127.0.0.1:4443")?,
            client_dir: lookup(CLIENT_DIR)
                .map_or_else(|| PathBuf::from("client/dist"), PathBuf::from),
            impairment: Impairment {
                delay: Duration::from_secs_f64(number(IMPAIR_DELAY_MS, 10_000.0)? / 1_000.0),
                jitter: Duration::from_secs_f64(number(IMPAIR_JITTER_MS, 10_000.0)? / 1_000.0),
                loss: number(IMPAIR_LOSS_PERCENT, 100.0)? / 100.0,
            },
            settings: Settings {
                template: match lookup(SITE).as_deref() {
                    None | Some("frontier") => FRONTIER_SITE,
                    Some("compact") => COMPACT_SITE,
                    Some(other) => {
                        return Err(ConfigError {
                            name: SITE,
                            expected: "frontier or compact",
                            value: other.to_owned(),
                        });
                    }
                },
                open_tiers: whole(&lookup, TIERS, 1, 3)?.unwrap_or(1),
                seed: whole(&lookup, SEED, 0, u32::MAX)?,
                opening_threat: whole(&lookup, NPCS, 0, MAX_NPCS as u32)?,
            },
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

    #[test]
    fn reads_the_launch_settings() {
        let defaults = Config::from_lookup(|_| None).unwrap().settings;
        assert_eq!(defaults.template, FRONTIER_SITE);
        assert_eq!(defaults.open_tiers, 1);
        assert_eq!(defaults.seed, None);
        assert_eq!(defaults.opening_threat, None);
        let set = Config::from_lookup(|name| match name {
            SITE => Some("compact".into()),
            TIERS => Some("3".into()),
            SEED => Some("42".into()),
            NPCS => Some("24".into()),
            _ => None,
        })
        .unwrap()
        .settings;
        assert_eq!(set.template, COMPACT_SITE);
        assert_eq!(set.open_tiers, 3);
        assert_eq!(set.seed, Some(42));
        assert_eq!(set.opening_threat, Some(24));
        for (name, value) in [(SITE, "huge"), (TIERS, "4"), (TIERS, "0"), (NPCS, "25")] {
            let value = value.to_owned();
            let result = Config::from_lookup(move |n| (n == name).then(|| value.clone()));
            assert!(result.is_err(), "{name}");
        }
    }
}
