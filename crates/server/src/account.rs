//! A pilot's account document: what their home holds (design sections 6 and 10).
//!
//! Documents are versioned JSON and carry the world they belong to, so a save
//! can never be loaded into another world by mistake. [`Account::from_json`]
//! reads every earlier version and upgrades it; [`Account::to_json`] always
//! writes the current one. Stage 1 data may be wiped, but saves are still
//! versioned (AGENTS.md section 8).

use serde::{Deserialize, Serialize};

/// The version [`Account::to_json`] writes.
pub const DOCUMENT_VERSION: u32 = 2;

/// Credits a new pilot starts with (design section 10).
pub const STARTING_CREDITS: u32 = 100;

/// Raw materials held at home, banked from runs.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct Stock {
    pub ore: u32,
    pub salvage: u32,
    pub volatiles: u32,
}

/// A facility tier the pilot has bought: theirs for good (design section 2).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Unlock {
    pub facility: String,
    pub tier: u32,
}

/// A facility installed in a socket of the outpost.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Installed {
    pub socket: String,
    pub facility: String,
    pub tier: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Account {
    pub world: String,
    pub callsign: String,
    pub credits: u32,
    pub stock: Stock,
    /// Everything ever bought, kept apart from the layout (design section 10):
    /// capabilities come from what is installed and powered, not from this.
    /// Empty until the outpost exists (plan M5).
    pub unlocks: Vec<Unlock>,
    /// What is installed in each socket. Empty until plan M5.
    pub layout: Vec<Installed>,
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

/// Version 2: the account as it is.
#[derive(Serialize, Deserialize)]
struct V2 {
    version: u32,
    #[serde(flatten)]
    account: Account,
}

/// Version 1: credits and stock side by side, with no unlocks or layout.
#[derive(Deserialize)]
struct V1 {
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
    /// A new pilot's home in `world`, empty: the starting balance arrives
    /// through the ledger (crate::store::Store::create_account).
    #[must_use]
    pub fn new(world: &str, callsign: &str) -> Self {
        Self {
            world: world.to_owned(),
            callsign: callsign.to_owned(),
            credits: 0,
            stock: Stock::default(),
            unlocks: Vec::new(),
            layout: Vec::new(),
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
                    unlocks: Vec::new(),
                    layout: Vec::new(),
                }
            }
            2 => serde_json::from_str::<V2>(json)?.account,
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
        serde_json::to_string(&V2 {
            version: DOCUMENT_VERSION,
            account: self.clone(),
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
    fn a_new_document_is_empty_until_the_ledger_grants_the_balance() {
        let account = Account::new("w", "Vega");
        assert_eq!(account.credits, 0);
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
    fn loads_a_version_1_document_as_the_previous_build_wrote_it() {
        // Written by commit 4f88154, the first build with accounts.
        let v1 = r#"{"version":1,"world":"w","callsign":"Vega","credits":60,"ore":12,"salvage":0,"volatiles":3}"#;
        let account = Account::from_json(v1, "w").unwrap();
        assert_eq!(account.callsign, "Vega");
        assert_eq!(account.credits, 60);
        assert_eq!(
            account.stock,
            Stock {
                ore: 12,
                salvage: 0,
                volatiles: 3
            }
        );
        assert!(account.unlocks.is_empty() && account.layout.is_empty());
        // Saved again, it is written at the current version.
        assert!(account.to_json().unwrap().contains(r#""version":2"#));
    }

    #[test]
    fn keeps_unlocks_apart_from_the_layout() {
        let mut account = Account::new("w", "Vega");
        account.unlocks.push(Unlock {
            facility: "refinery".into(),
            tier: 2,
        });
        account.layout.push(Installed {
            socket: "a".into(),
            facility: "refinery".into(),
            tier: 1,
        });
        let json = account.to_json().unwrap();
        assert!(json.contains(r#""unlocks":[{"facility":"refinery","tier":2}]"#));
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
