//! Server settings, read from environment variables with local defaults.

use std::net::SocketAddr;
use std::path::PathBuf;
use std::time::Duration;

use frontier_sim::goods::Good;
use frontier_sim::site::{COMPACT_SITE, FRONTIER_SITE};

use crate::ledger::Item;
use frontier_sim::skirmish::MAX_NPCS;

use webauthn_rs::prelude::Url;

use crate::relay::Impairment;
use crate::transport::{SeedPool, Settings};

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
    /// What every launch offers and how.
    pub settings: Settings,
    /// The SQLite file that holds the world's saves.
    pub data_path: PathBuf,
    /// The origin pages are served from, as browsers see it, when it is not
    /// `http://localhost` on the HTTP port. Passkeys are bound to it, and need
    /// a domain name: an IP address cannot be a relying party.
    pub origin: Option<Url>,
    /// Stock every new pilot gets on top of the starting balance, for trying
    /// facilities without flying for them first: `FRONTIER_GRANT`, such as
    /// `1000cr,40ore,20alloy,10components`.
    pub grant: Vec<(Item, u32)>,
}

/// Reads a grant such as `1000cr,40ore`: amounts, each followed by `cr` or
/// a good's name.
#[must_use]
pub fn parse_grant(value: &str) -> Option<Vec<(Item, u32)>> {
    value
        .split(',')
        .map(str::trim)
        .filter(|part| !part.is_empty())
        .map(|part| {
            let split = part.find(|c: char| !c.is_ascii_digit())?;
            let (amount, name) = part.split_at(split);
            let amount: u32 = amount.parse().ok().filter(|&amount| amount > 0)?;
            let item = match name.trim() {
                "cr" => Item::Credits,
                name => Item::Good(Good::ALL.into_iter().find(|good| good.name() == name)?),
            };
            Some((item, amount))
        })
        .collect()
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
const SITE: &str = "FRONTIER_SITE";
const TIERS: &str = "FRONTIER_TIERS";
const SEED: &str = "FRONTIER_SEED";
const SEEDS: &str = "FRONTIER_SEEDS";
const NPCS: &str = "FRONTIER_NPCS";
const DATA: &str = "FRONTIER_DATA";
const ORIGIN: &str = "FRONTIER_ORIGIN";
const GRANT: &str = "FRONTIER_GRANT";
const EXTRACT_RATE: &str = "FRONTIER_EXTRACT_RATE";

/// Reads a multiple of the design's extraction rate, such as 2 or 1.5, as
/// whole percent, if the variable is set: from a tenth to ten times. Empty is
/// unset, as Docker Compose passes a variable the shell did not give.
fn extract_rate(lookup: &impl Fn(&str) -> Option<String>) -> Result<Option<u32>, ConfigError> {
    lookup(EXTRACT_RATE)
        .filter(|value| !value.trim().is_empty())
        .map(|value| {
            value
                .trim()
                .parse::<f64>()
                .ok()
                .filter(|times| (0.1..=10.0).contains(times))
                // In range, so the percent fits a u32 and is positive.
                .map(|times| (times * 100.0).round() as u32)
                .ok_or(ConfigError::Invalid {
                    name: EXTRACT_RATE,
                    expected: "a multiple of the extraction rate from 0.1 to 10, such as 2",
                    value,
                })
        })
        .transpose()
}

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
                .ok_or(ConfigError::Invalid {
                    name,
                    expected: "a whole number in range",
                    value,
                })
        })
        .transpose()
}

/// Reads a comma-separated list of seeds, if the variable is set.
fn seed_pool(lookup: &impl Fn(&str) -> Option<String>) -> Result<SeedPool, ConfigError> {
    let Some(value) = lookup(SEEDS) else {
        return Ok(SeedPool::default());
    };
    let seeds: Option<Vec<u32>> = value
        .split(',')
        .map(|seed| seed.trim().parse::<u32>().ok())
        .collect();
    seeds
        .and_then(|seeds| SeedPool::new(&seeds))
        .ok_or(ConfigError::Invalid {
            name: SEEDS,
            expected: "1 to 16 whole numbers separated by commas",
            value,
        })
}

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
            settings: Settings {
                template: match lookup(SITE).as_deref() {
                    None | Some("frontier") => FRONTIER_SITE,
                    Some("compact") => COMPACT_SITE,
                    Some(other) => {
                        return Err(ConfigError::Invalid {
                            name: SITE,
                            expected: "frontier or compact",
                            value: other.to_owned(),
                        });
                    }
                },
                open_tiers: whole(&lookup, TIERS, 1, 3)?.unwrap_or(1),
                seed: whole(&lookup, SEED, 0, u32::MAX)?,
                seed_pool: seed_pool(&lookup)?,
                opening_threat: whole(&lookup, NPCS, 0, MAX_NPCS as u32)?,
                extract_percent: extract_rate(&lookup)?,
            },
            data_path: lookup(DATA)
                .map_or_else(|| PathBuf::from("data/frontier.db"), PathBuf::from),
            origin: lookup(ORIGIN)
                .map(|value| {
                    Url::parse(&value)
                        .ok()
                        .filter(|url| url.domain().is_some())
                        .ok_or(ConfigError::Invalid {
                            name: ORIGIN,
                            expected: "a URL with a domain name, such as http://localhost:8080",
                            value,
                        })
                })
                .transpose()?,
            grant: match lookup(GRANT) {
                None => Vec::new(),
                Some(value) => parse_grant(&value).ok_or(ConfigError::Invalid {
                    name: GRANT,
                    expected: "amounts such as 1000cr,40ore,20alloy",
                    value,
                })?,
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

    /// The origin browsers load the game from, given the HTTP address the
    /// server actually listens on: the configured one, or `http://localhost`
    /// on that port.
    #[must_use]
    pub fn origin(&self, http: SocketAddr) -> Url {
        self.origin.clone().unwrap_or_else(|| {
            Url::parse(&format!("http://localhost:{}", http.port()))
                .expect("localhost with a port is a valid URL")
        })
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
        assert_eq!(defaults.extract_percent, None);
        let empty = Config::from_lookup(|n| (n == EXTRACT_RATE).then(String::new)).unwrap();
        assert_eq!(empty.settings.extract_percent, None);
        for (value, percent) in [("2", 200), ("1.5", 150), (" 3 ", 300), ("0.1", 10)] {
            let value = value.to_owned();
            let read = Config::from_lookup(move |n| (n == EXTRACT_RATE).then(|| value.clone()))
                .unwrap()
                .settings;
            assert_eq!(read.extract_percent, Some(percent));
        }
        for bad in ["0", "11", "fast", "-2", "NaN"] {
            let value = bad.to_owned();
            let result = Config::from_lookup(move |n| (n == EXTRACT_RATE).then(|| value.clone()));
            assert!(result.is_err(), "{EXTRACT_RATE}={bad}");
        }
        let pooled = Config::from_lookup(|name| (name == SEEDS).then(|| "29, 41,77".into()))
            .unwrap()
            .settings;
        assert_eq!(pooled.seed_pool, SeedPool::new(&[29, 41, 77]).unwrap());
        assert_eq!(defaults.seed_pool, SeedPool::default());
        for bad in ["", "1,x", "1,,2", &vec!["3"; 17].join(",")] {
            let bad = bad.to_owned();
            let result = Config::from_lookup(move |n| (n == SEEDS).then(|| bad.clone()));
            assert!(result.is_err(), "{SEEDS}");
        }
        for (name, value) in [(SITE, "huge"), (TIERS, "4"), (TIERS, "0"), (NPCS, "25")] {
            let value = value.to_owned();
            let result = Config::from_lookup(move |n| (n == name).then(|| value.clone()));
            assert!(result.is_err(), "{name}");
        }
    }

    #[test]
    fn reads_the_data_path_and_origin() {
        let defaults = Config::from_lookup(|_| None).unwrap();
        assert_eq!(defaults.data_path, PathBuf::from("data/frontier.db"));
        assert_eq!(
            defaults.origin("127.0.0.1:43123".parse().unwrap()).as_str(),
            "http://localhost:43123/",
            "localhost on the port actually bound"
        );
        let set = Config::from_lookup(|name| match name {
            DATA => Some("/var/lib/frontier/world.db".into()),
            ORIGIN => Some("https://outpost.example".into()),
            _ => None,
        })
        .unwrap();
        assert_eq!(set.data_path, PathBuf::from("/var/lib/frontier/world.db"));
        assert_eq!(
            set.origin("0.0.0.0:8080".parse().unwrap()).as_str(),
            "https://outpost.example/"
        );
        assert!(
            Config::from_lookup(|name| (name == ORIGIN).then(|| "http://127.0.0.1:8080".into()))
                .is_err(),
            "passkeys cannot use an IP address"
        );
    }

    #[test]
    fn reads_a_grant_for_new_pilots() {
        let config = Config::from_lookup(|name| {
            (name == GRANT).then(|| "1000cr, 40ore,20alloy,10components".to_owned())
        })
        .unwrap();
        assert_eq!(
            config.grant,
            [
                (Item::Credits, 1_000),
                (Item::ORE, 40),
                (Item::Good(Good::Alloy), 20),
                (Item::Good(Good::Components), 10),
            ]
        );
        assert!(Config::from_lookup(|_| None).unwrap().grant.is_empty());
        for bad in ["40 bananas", "cr", "0ore", "ore40"] {
            let error = Config::from_lookup(|name| (name == GRANT).then(|| bad.to_owned()));
            assert!(error.is_err(), "{bad}");
        }
    }
}
