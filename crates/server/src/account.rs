//! A pilot's account document: what their home holds (design sections 6 and 10).
//!
//! Documents are versioned JSON and carry the world they belong to, so a save
//! can never be loaded into another world by mistake. [`Account::from_json`]
//! reads every earlier version and upgrades it; [`Account::to_json`] always
//! writes the current one. Stage 1 data may be wiped, but saves are still
//! versioned (AGENTS.md section 8).

use serde::{Deserialize, Serialize};

/// The version [`Account::to_json`] writes.
pub const DOCUMENT_VERSION: u32 = 1;

/// Credits a new pilot starts with (design section 10).
pub const STARTING_CREDITS: u32 = 100;

/// Raw materials held at home, banked from runs.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct Stock {
    pub ore: u32,
    pub salvage: u32,
    pub volatiles: u32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Account {
    pub world: String,
    pub callsign: String,
    pub credits: u32,
    pub stock: Stock,
}

/// Why a document could not be read.
#[derive(Debug, thiserror::Error)]
pub enum DocumentError {
    #[error("the document is not valid JSON for its version: {0}")]
    Malformed(#[from] serde_json::Error),
    #[error("document version {0} is newer than this server reads ({DOCUMENT_VERSION})")]
    TooNew(u32),
    #[error("the document belongs to world {found}, not {expected}")]
    WrongWorld { expected: String, found: String },
}

/// Version 1: credits and stock side by side.
#[derive(Serialize, Deserialize)]
struct V1 {
    version: u32,
    world: String,
    callsign: String,
    credits: u32,
    ore: u32,
    salvage: u32,
    volatiles: u32,
}

#[derive(Deserialize)]
struct Versioned {
    version: u32,
}

impl Account {
    /// A new pilot's home in `world`.
    #[must_use]
    pub fn new(world: &str, callsign: &str) -> Self {
        Self {
            world: world.to_owned(),
            callsign: callsign.to_owned(),
            credits: STARTING_CREDITS,
            stock: Stock::default(),
        }
    }

    /// Reads a document of any version up to [`DOCUMENT_VERSION`] that belongs
    /// to `world`.
    ///
    /// # Errors
    /// Fails if the document is malformed, newer than this server, or from
    /// another world.
    pub fn from_json(json: &str, world: &str) -> Result<Self, DocumentError> {
        let Versioned { version } = serde_json::from_str(json)?;
        let account = match version {
            1 => {
                let v1: V1 = serde_json::from_str(json)?;
                Self {
                    world: v1.world,
                    callsign: v1.callsign,
                    credits: v1.credits,
                    stock: Stock {
                        ore: v1.ore,
                        salvage: v1.salvage,
                        volatiles: v1.volatiles,
                    },
                }
            }
            newer => return Err(DocumentError::TooNew(newer)),
        };
        if account.world != world {
            return Err(DocumentError::WrongWorld {
                expected: world.to_owned(),
                found: account.world,
            });
        }
        Ok(account)
    }

    /// Writes the document at [`DOCUMENT_VERSION`].
    ///
    /// # Errors
    /// Cannot fail for these types; the error is serde's.
    pub fn to_json(&self) -> Result<String, serde_json::Error> {
        serde_json::to_string(&V1 {
            version: DOCUMENT_VERSION,
            world: self.world.clone(),
            callsign: self.callsign.clone(),
            credits: self.credits,
            ore: self.stock.ore,
            salvage: self.stock.salvage,
            volatiles: self.stock.volatiles,
        })
    }
}

/// Whether `callsign` is acceptable: 3 to 16 letters, digits, `-` or `_`.
#[must_use]
pub fn valid_callsign(callsign: &str) -> bool {
    (3..=16).contains(&callsign.len())
        && callsign
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_new_pilot_starts_with_the_starting_balance_and_nothing_else() {
        let account = Account::new("w", "Vega");
        assert_eq!(account.credits, 100);
        assert_eq!(account.stock, Stock::default());
    }

    #[test]
    fn round_trips_through_the_current_version() {
        let mut account = Account::new("w", "Vega");
        account.stock.ore = 12;
        let json = account.to_json().unwrap();
        assert!(json.contains(&format!(r#""version":{DOCUMENT_VERSION}"#)));
        assert_eq!(Account::from_json(&json, "w").unwrap(), account);
    }

    #[test]
    fn refuses_a_document_from_another_world_or_a_newer_server() {
        let json = Account::new("w", "Vega").to_json().unwrap();
        assert!(matches!(
            Account::from_json(&json, "other"),
            Err(DocumentError::WrongWorld { .. })
        ));
        let newer = json.replace(
            &format!(r#""version":{DOCUMENT_VERSION}"#),
            r#""version":99"#,
        );
        assert!(matches!(
            Account::from_json(&newer, "w"),
            Err(DocumentError::TooNew(99))
        ));
    }

    #[test]
    fn accepts_only_short_plain_callsigns() {
        assert!(valid_callsign("Vega-7"));
        assert!(!valid_callsign("Vo"));
        assert!(!valid_callsign("a name with spaces"));
        assert!(!valid_callsign("seventeen-chars-x"));
    }
}
