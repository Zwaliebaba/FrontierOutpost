//! A pilot's account document: what their home holds (design sections 6 and 10).
//!
//! Documents are versioned JSON and carry the world they belong to, so a save
//! can never be loaded into another world by mistake. [`Account::from_json`]
//! reads every earlier version and upgrades it; [`Account::to_json`] always
//! writes the current one. Stage 1 data may be wiped, but saves are still
//! versioned (AGENTS.md section 8).

use frontier_sim::fitting::Hangar;
use frontier_sim::goods::Good;
use frontier_sim::market::Queue;
use frontier_sim::outpost::{self, Outpost};
use serde::{Deserialize, Serialize};

/// The version [`Account::to_json`] writes.
pub const DOCUMENT_VERSION: u32 = 5;

/// Credits a new pilot starts with (design section 10).
pub const STARTING_CREDITS: u32 = 100;

/// Goods held at home: raw materials banked from runs, and goods processed
/// from them.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct Stock {
    pub ore: u32,
    pub salvage: u32,
    pub volatiles: u32,
    pub alloy: u32,
    pub components: u32,
}

impl Stock {
    #[must_use]
    pub const fn get(&self, good: Good) -> u32 {
        match good {
            Good::Ore => self.ore,
            Good::Salvage => self.salvage,
            Good::Volatiles => self.volatiles,
            Good::Alloy => self.alloy,
            Good::Components => self.components,
        }
    }

    pub fn get_mut(&mut self, good: Good) -> &mut u32 {
        match good {
            Good::Ore => &mut self.ore,
            Good::Salvage => &mut self.salvage,
            Good::Volatiles => &mut self.volatiles,
            Good::Alloy => &mut self.alloy,
            Good::Components => &mut self.components,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Account {
    pub world: String,
    pub callsign: String,
    pub credits: u32,
    pub stock: Stock,
    /// Unlocks, kept apart from the layout (design section 10): capabilities
    /// come from what is installed and powered, not from what was bought.
    #[serde(flatten)]
    pub outpost: Outpost,
    /// Batches waiting at the processor; their inputs have left the stock.
    pub processing: Queue,
    /// Rigs and modules owned, and the saved fitting.
    pub hangar: Hangar,
    /// Supplies bought for a run that docked without the contract being
    /// paid: they are still aboard, and the next launch costs nothing
    /// (design section 10). Absent from documents before version 4.
    #[serde(default)]
    pub supplies_carried: bool,
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

/// Version 5: the account as it is. Version 4 is the same document without
/// the time the processor's batch in hand was started, so its batches start
/// when the processor is next looked at; version 3 also lacks
/// `supplies_carried`, which then reads as false.
#[derive(Serialize, Deserialize)]
struct V5 {
    version: u32,
    #[serde(flatten)]
    account: Account,
}

/// Version 2: three raw materials, and unlocks and a layout that stayed empty
/// before the outpost existed.
#[derive(Deserialize)]
struct V2 {
    world: String,
    callsign: String,
    credits: u32,
    stock: V2Stock,
    unlocks: Vec<outpost::Unlock>,
    layout: Vec<V2Installed>,
}

#[derive(Deserialize)]
struct V2Stock {
    ore: u32,
    salvage: u32,
    volatiles: u32,
}

#[derive(Deserialize)]
struct V2Installed {
    socket: String,
    facility: String,
    tier: u32,
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
            outpost: Outpost::default(),
            processing: Queue::default(),
            hangar: Hangar::default(),
            supplies_carried: false,
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
                let mut account = Self::new(&v1.world, &v1.callsign);
                account.credits = v1.credits;
                account.stock.ore = v1.ore;
                account.stock.salvage = v1.salvage;
                account.stock.volatiles = v1.volatiles;
                account
            }
            2 => {
                let v2: V2 = serde_json::from_str(json)?;
                let mut account = Self::new(&v2.world, &v2.callsign);
                account.credits = v2.credits;
                account.stock.ore = v2.stock.ore;
                account.stock.salvage = v2.stock.salvage;
                account.stock.volatiles = v2.stock.volatiles;
                account.outpost.unlocks = v2.unlocks;
                // Version 2 recorded the tier installed; it ran at that tier.
                account.outpost.layout = v2
                    .layout
                    .into_iter()
                    .map(|installed| outpost::Installed {
                        socket: installed.socket,
                        facility: installed.facility,
                        run: installed.tier,
                    })
                    .collect();
                account
            }
            3..=5 => serde_json::from_str::<V5>(json)?.account,
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
        serde_json::to_string(&V5 {
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
                volatiles: 3,
                ..Stock::default()
            }
        );
        assert_eq!(account.outpost, Outpost::default());
        // Saved again, it is written at the current version.
        assert!(
            account
                .to_json()
                .unwrap()
                .contains(&format!(r#""version":{DOCUMENT_VERSION}"#))
        );
    }

    #[test]
    fn loads_a_version_2_document_as_the_previous_build_wrote_it() {
        // Written by commit 044ede5, the last build before the outpost. Its
        // unlocks and layout were always empty; a layout is upgraded anyway.
        let v2 = r#"{"version":2,"world":"w","callsign":"Vega","credits":220,"stock":{"ore":20,"salvage":1,"volatiles":0},"unlocks":[{"facility":"refinery","tier":1}],"layout":[{"socket":"a","facility":"refinery","tier":1}]}"#;
        let account = Account::from_json(v2, "w").unwrap();
        assert_eq!(account.credits, 220);
        assert_eq!(account.stock.ore, 20);
        assert_eq!(account.stock.alloy, 0);
        assert_eq!(account.outpost.owned("refinery"), 1);
        assert_eq!(account.outpost.installed("a").unwrap().run, 1);
        assert_eq!(account.hangar, Hangar::default());
        let empty = r#"{"version":2,"world":"w","callsign":"Vega","credits":100,"stock":{"ore":0,"salvage":0,"volatiles":0},"unlocks":[],"layout":[]}"#;
        assert_eq!(
            Account::from_json(empty, "w").unwrap().outpost,
            Outpost::default()
        );
    }

    #[test]
    fn loads_a_version_3_document_as_the_previous_build_wrote_it() {
        // Written by commit a229f24, the last build before supplies carried
        // over from an unpaid dock.
        let v3 = r#"{"version":3,"world":"w","callsign":"Vega","credits":110,"stock":{"ore":4,"salvage":0,"volatiles":0,"alloy":0,"components":0},"unlocks":[{"facility":"refinery","tier":1}],"layout":[{"socket":"a","facility":"refinery","run":1}],"processing":{"batches":[{"recipe":"alloy","batches":3}]},"hangar":{"pieces":[{"id":0,"fitting":"extraction_rig","wear":0}],"next_id":1,"saved":{"rig":null,"modules":[]}}}"#;
        let account = Account::from_json(v3, "w").unwrap();
        assert_eq!(account.credits, 110);
        assert_eq!(account.stock.ore, 4);
        assert_eq!(account.outpost.installed("a").unwrap().run, 1);
        assert_eq!(account.processing.waiting("alloy"), 3);
        assert_eq!(account.hangar.count("extraction_rig"), 1);
        assert!(!account.supplies_carried, "nothing was carried before");
        assert_eq!(account.processing.started, None, "nor batches timed");
        let mut carried = account;
        carried.supplies_carried = true;
        let json = carried.to_json().unwrap();
        assert!(json.contains(&format!(r#""version":{DOCUMENT_VERSION}"#)));
        assert_eq!(Account::from_json(&json, "w").unwrap(), carried);
    }

    #[test]
    fn keeps_unlocks_apart_from_the_layout() {
        let mut account = Account::new("w", "Vega");
        account.outpost.unlocks.push(outpost::Unlock {
            facility: "refinery".into(),
            tier: 2,
        });
        account.outpost.layout.push(outpost::Installed {
            socket: "a".into(),
            facility: "refinery".into(),
            run: 1,
        });
        account.processing.add("alloy", 3, 1_800_000_000);
        account
            .hangar
            .add(frontier_sim::fitting::fitting("extraction_rig").unwrap());
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
