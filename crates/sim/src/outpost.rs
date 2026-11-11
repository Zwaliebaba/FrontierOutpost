//! The outpost (design sections 2 and 10): facilities in construction sockets,
//! bounded by power, granting capability tags to the expedition.
//!
//! Everything here is data ([`OutpostRules`]) and pure functions on a home's
//! [`Outpost`]. Unlocks are permanent and kept apart from the active layout;
//! capabilities come from the layout as installed and powered, never from the
//! unlocks. Every number in [`STAGE1`] is a placeholder from design section 10.

use serde::{Deserialize, Serialize};

use crate::goods::{Cost, Good};
use crate::site::Capability;

/// When a socket can take a facility.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SocketOpens {
    AtStart,
    /// Once the first facility is installed (design section 10: socket B).
    WithFirstFacility,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SocketRule {
    pub id: &'static str,
    pub name: &'static str,
    /// The part of the station it sits beside, which adjacency refers to.
    pub beside: &'static str,
    pub opens: SocketOpens,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TierRule {
    /// The purchase price, which includes the first installation.
    pub cost: Cost,
    /// Power drawn while running at this tier.
    pub draw: u32,
    /// What running at this tier adds to the tiers below it.
    pub grants: &'static [Capability],
    /// Processor batches per run while running at this tier, replacing the
    /// core's own; `None` leaves the processor alone.
    pub processor_batches: Option<u32>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FacilityRule {
    pub id: &'static str,
    pub name: &'static str,
    /// Tier 1 first.
    pub tiers: &'static [TierRule],
    /// The rig given with the first purchase (design section 10: "the first
    /// rig comes with its facility"), by fitting identifier.
    pub rig: Option<&'static str>,
}

/// What a facility gains from the socket it sits in.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Bonus {
    ProcessorBatches(u32),
    /// Rig and module prices, purchase and replacement, fall by this percentage.
    FittingDiscount(u32),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AdjacencyRule {
    pub facility: &'static str,
    pub socket: &'static str,
    pub bonus: Bonus,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct OutpostRules {
    pub power_cap: u32,
    /// The core processor's batches per run.
    pub processor_batches: u32,
    pub sockets: &'static [SocketRule],
    pub facilities: &'static [FacilityRule],
    pub adjacency: &'static [AdjacencyRule],
    /// Installing a facility again after removing it.
    pub reinstall: Cost,
    /// The highest threat tier any home can open.
    pub top_threat_tier: u32,
}

const TIER_2_COST: Cost = Cost {
    credits: 550,
    goods: &[(Good::Alloy, 12), (Good::Components, 5)],
};
const TIER_1_COST: Cost = Cost {
    credits: 150,
    goods: &[(Good::Ore, 10)],
};

/// The Stage 1 outpost of design section 10.
pub const STAGE1: OutpostRules = OutpostRules {
    power_cap: 5,
    processor_batches: 4,
    sockets: &[
        SocketRule {
            id: "a",
            name: "Socket A",
            beside: "cargo bay",
            opens: SocketOpens::AtStart,
        },
        SocketRule {
            id: "b",
            name: "Socket B",
            beside: "berth",
            opens: SocketOpens::WithFirstFacility,
        },
    ],
    facilities: &[
        FacilityRule {
            id: "sensor_array",
            name: "Sensor array",
            tiers: &[
                TierRule {
                    cost: TIER_1_COST,
                    draw: 2,
                    grants: &[Capability::LocateSignal, Capability::ScoutRig],
                    processor_batches: None,
                },
                TierRule {
                    cost: TIER_2_COST,
                    draw: 3,
                    grants: &[Capability::DeepScan],
                    processor_batches: None,
                },
            ],
            rig: Some("scout_rig"),
        },
        FacilityRule {
            id: "refinery",
            name: "Refinery",
            tiers: &[
                TierRule {
                    cost: TIER_1_COST,
                    draw: 2,
                    grants: &[
                        Capability::WorkVolatiles,
                        Capability::ExtractionRig,
                        Capability::CargoFrame,
                    ],
                    processor_batches: Some(8),
                },
                TierRule {
                    cost: TIER_2_COST,
                    draw: 3,
                    grants: &[Capability::HeatSink],
                    processor_batches: Some(14),
                },
            ],
            rig: Some("extraction_rig"),
        },
        FacilityRule {
            id: "fabrication_bay",
            name: "Fabrication bay",
            tiers: &[
                TierRule {
                    cost: TIER_1_COST,
                    draw: 2,
                    grants: &[Capability::BreachNest, Capability::EscortRig],
                    processor_batches: None,
                },
                TierRule {
                    cost: TIER_2_COST,
                    draw: 3,
                    grants: &[Capability::PointDefence],
                    processor_batches: None,
                },
            ],
            rig: Some("escort_rig"),
        },
    ],
    adjacency: &[
        AdjacencyRule {
            facility: "refinery",
            socket: "a",
            bonus: Bonus::ProcessorBatches(2),
        },
        AdjacencyRule {
            facility: "fabrication_bay",
            socket: "b",
            bonus: Bonus::FittingDiscount(20),
        },
    ],
    reinstall: Cost {
        credits: 40,
        goods: &[(Good::Alloy, 6)],
    },
    top_threat_tier: 3,
};

/// A facility tier a home has bought: theirs for good.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Unlock {
    pub facility: String,
    /// The highest tier bought.
    pub tier: u32,
}

/// A facility installed in a socket, at the tier it owns.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Installed {
    pub socket: String,
    pub facility: String,
    /// The tier it runs at: at most the tier owned, and 0 when unpowered.
    pub run: u32,
}

/// A home's facilities: what it has bought and what is installed where.
#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct Outpost {
    pub unlocks: Vec<Unlock>,
    pub layout: Vec<Installed>,
}

impl Outpost {
    /// The highest tier of `facility` bought, or 0.
    #[must_use]
    pub fn owned(&self, facility: &str) -> u32 {
        self.unlocks
            .iter()
            .find(|unlock| unlock.facility == facility)
            .map_or(0, |unlock| unlock.tier)
    }

    /// What is installed in `socket`.
    #[must_use]
    pub fn installed(&self, socket: &str) -> Option<&Installed> {
        self.layout
            .iter()
            .find(|installed| installed.socket == socket)
    }

    fn installed_mut(&mut self, socket: &str) -> Option<&mut Installed> {
        self.layout
            .iter_mut()
            .find(|installed| installed.socket == socket)
    }
}

/// What an action does.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Action {
    /// Tier 1 of a facility never bought.
    Bought,
    /// A facility bought before, installed again.
    Reinstalled,
    Upgraded,
    Removed,
    Powered,
}

/// The outcome of an action that the rules allow: the home as it becomes,
/// and what it costs. The caller pays the cost through the ledger and saves
/// the home in the same transaction.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Change {
    pub action: Action,
    pub outpost: Outpost,
    pub cost: Cost,
    /// The rig that comes with a first purchase.
    pub rig: Option<&'static str>,
}

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum Refusal {
    #[error("there is no socket {0}")]
    NoSuchSocket(String),
    #[error("there is no facility {0}")]
    NoSuchFacility(String),
    #[error("{0} opens when the first facility is installed")]
    SocketClosed(&'static str),
    #[error("{0} already holds a facility")]
    SocketTaken(&'static str),
    #[error("{0} is empty")]
    SocketEmpty(&'static str),
    #[error("the {0} is already installed")]
    AlreadyInstalled(&'static str),
    #[error("the {0} is at its top tier")]
    TopTier(&'static str),
    #[error("the {facility} owns tier {owned}, not tier {asked}")]
    TierNotOwned {
        facility: &'static str,
        owned: u32,
        asked: u32,
    },
    #[error("running at tier {tier} draws {draw} power, and only {free} is free")]
    PowerShort { tier: u32, draw: u32, free: u32 },
}

impl OutpostRules {
    /// The socket called `id`.
    ///
    /// # Errors
    /// Fails if there is none.
    pub fn socket(&self, id: &str) -> Result<&'static SocketRule, Refusal> {
        self.sockets
            .iter()
            .find(|socket| socket.id == id)
            .ok_or_else(|| Refusal::NoSuchSocket(id.to_owned()))
    }

    /// The facility called `id`.
    ///
    /// # Errors
    /// Fails if there is none.
    pub fn facility(&self, id: &str) -> Result<&'static FacilityRule, Refusal> {
        self.facilities
            .iter()
            .find(|facility| facility.id == id)
            .ok_or_else(|| Refusal::NoSuchFacility(id.to_owned()))
    }

    /// Whether `socket` can take a facility.
    #[must_use]
    pub fn is_open(&self, outpost: &Outpost, socket: &SocketRule) -> bool {
        match socket.opens {
            SocketOpens::AtStart => true,
            // A first facility, once bought, opens it for good.
            SocketOpens::WithFirstFacility => !outpost.unlocks.is_empty(),
        }
    }

    /// The power `installed` draws.
    fn draw(&self, installed: &Installed) -> u32 {
        self.facility(&installed.facility)
            .ok()
            .and_then(|facility| tier(facility, installed.run))
            .map_or(0, |tier| tier.draw)
    }

    /// Power drawn by the whole layout.
    #[must_use]
    pub fn power_used(&self, outpost: &Outpost) -> u32 {
        outpost
            .layout
            .iter()
            .map(|installed| self.draw(installed))
            .sum()
    }

    /// What the layout grants the expedition: every tier each facility runs
    /// at and those below it. An unpowered facility grants nothing.
    #[must_use]
    pub fn capabilities(&self, outpost: &Outpost) -> Vec<Capability> {
        let mut granted: Vec<Capability> = outpost
            .layout
            .iter()
            .filter_map(|installed| {
                let facility = self.facility(&installed.facility).ok()?;
                Some(running(facility, installed.run).flat_map(|tier| tier.grants.iter().copied()))
            })
            .flatten()
            .collect();
        granted.sort_unstable();
        granted.dedup();
        granted
    }

    fn bonuses(&self, outpost: &Outpost) -> impl Iterator<Item = Bonus> {
        self.adjacency
            .iter()
            .filter(|rule| {
                outpost.installed(rule.socket).is_some_and(|installed| {
                    installed.facility == rule.facility && installed.run > 0
                })
            })
            .map(|rule| rule.bonus)
    }

    /// Processor batches per completed run.
    #[must_use]
    pub fn processor_batches(&self, outpost: &Outpost) -> u32 {
        let facilities = outpost
            .layout
            .iter()
            .filter_map(|installed| {
                let facility = self.facility(&installed.facility).ok()?;
                running(facility, installed.run)
                    .filter_map(|tier| tier.processor_batches)
                    .last()
            })
            .max()
            .unwrap_or(self.processor_batches);
        let adjacency: u32 = self
            .bonuses(outpost)
            .map(|bonus| match bonus {
                Bonus::ProcessorBatches(extra) => extra,
                Bonus::FittingDiscount(_) => 0,
            })
            .sum();
        facilities + adjacency
    }

    /// The percentage off rig and module prices.
    #[must_use]
    pub fn fitting_discount(&self, outpost: &Outpost) -> u32 {
        self.bonuses(outpost)
            .map(|bonus| match bonus {
                Bonus::FittingDiscount(percent) => percent,
                Bonus::ProcessorBatches(_) => 0,
            })
            .sum::<u32>()
            .min(100)
    }

    /// The highest threat tier open to this home: tier II with the first
    /// facility owned at tier 2, tier III with the second (design section 10).
    /// Tiers follow unlocks, not the active layout.
    #[must_use]
    pub fn open_tiers(&self, outpost: &Outpost) -> u32 {
        let at_tier_2 = outpost
            .unlocks
            .iter()
            .filter(|unlock| unlock.tier >= 2)
            .count();
        (1 + u32::try_from(at_tier_2).unwrap_or(u32::MAX)).min(self.top_threat_tier)
    }

    /// Installs `facility` in `socket`: bought at tier 1 if it never was,
    /// installed again otherwise. It runs at the highest tier the free power
    /// allows, or unpowered if none fits.
    ///
    /// # Errors
    /// Fails if the socket is closed or taken, or the facility is installed.
    pub fn install(
        &self,
        outpost: &Outpost,
        socket: &str,
        facility: &str,
    ) -> Result<Change, Refusal> {
        let socket = self.socket(socket)?;
        let facility = self.facility(facility)?;
        if !self.is_open(outpost, socket) {
            return Err(Refusal::SocketClosed(socket.name));
        }
        if outpost.installed(socket.id).is_some() {
            return Err(Refusal::SocketTaken(socket.name));
        }
        if outpost
            .layout
            .iter()
            .any(|installed| installed.facility == facility.id)
        {
            return Err(Refusal::AlreadyInstalled(facility.name));
        }
        let mut next = outpost.clone();
        let owned = outpost.owned(facility.id);
        let (action, cost, rig) = if owned == 0 {
            next.unlocks.push(Unlock {
                facility: facility.id.to_owned(),
                tier: 1,
            });
            (Action::Bought, facility.tiers[0].cost, facility.rig)
        } else {
            (Action::Reinstalled, self.reinstall, None)
        };
        let run = self.best_run(&next, facility, next.owned(facility.id));
        next.layout.push(Installed {
            socket: socket.id.to_owned(),
            facility: facility.id.to_owned(),
            run,
        });
        Ok(Change {
            action,
            outpost: next,
            cost,
            rig,
        })
    }

    /// Buys the next tier of the facility in `socket`, and runs it at the
    /// highest tier the free power allows.
    ///
    /// # Errors
    /// Fails if the socket is empty or its facility is at its top tier.
    pub fn upgrade(&self, outpost: &Outpost, socket: &str) -> Result<Change, Refusal> {
        let socket = self.socket(socket)?;
        let installed = outpost
            .installed(socket.id)
            .ok_or(Refusal::SocketEmpty(socket.name))?;
        let facility = self.facility(&installed.facility)?;
        let owned = outpost.owned(facility.id);
        let next_tier = tier(facility, owned + 1).ok_or(Refusal::TopTier(facility.name))?;
        let mut next = outpost.clone();
        if let Some(unlock) = next
            .unlocks
            .iter_mut()
            .find(|unlock| unlock.facility == facility.id)
        {
            unlock.tier = owned + 1;
        }
        // Its own draw is freed first: an upgrade replaces what it ran at.
        let mut others = next.clone();
        others.layout.retain(|other| other.socket != socket.id);
        let run = self.best_run(&others, facility, owned + 1);
        if let Some(installed) = next.installed_mut(socket.id) {
            installed.run = run;
        }
        Ok(Change {
            action: Action::Upgraded,
            outpost: next,
            cost: next_tier.cost,
            rig: None,
        })
    }

    /// Removes the facility in `socket`, for nothing. Its unlock stays.
    ///
    /// # Errors
    /// Fails if the socket is empty.
    pub fn remove(&self, outpost: &Outpost, socket: &str) -> Result<Change, Refusal> {
        let socket = self.socket(socket)?;
        if outpost.installed(socket.id).is_none() {
            return Err(Refusal::SocketEmpty(socket.name));
        }
        let mut next = outpost.clone();
        next.layout
            .retain(|installed| installed.socket != socket.id);
        Ok(Change {
            action: Action::Removed,
            outpost: next,
            cost: Cost::FREE,
            rig: None,
        })
    }

    /// Runs the facility in `socket` at `run`, from 0 (unpowered) to the tier
    /// it owns: a tier 2 facility may run at tier 1 draw with tier 1
    /// capabilities (design section 10).
    ///
    /// # Errors
    /// Fails if the socket is empty, the tier is not owned, or the power is
    /// short.
    pub fn power(&self, outpost: &Outpost, socket: &str, run: u32) -> Result<Change, Refusal> {
        let socket = self.socket(socket)?;
        let installed = outpost
            .installed(socket.id)
            .ok_or(Refusal::SocketEmpty(socket.name))?;
        let facility = self.facility(&installed.facility)?;
        let owned = outpost.owned(facility.id);
        if run > owned {
            return Err(Refusal::TierNotOwned {
                facility: facility.name,
                owned,
                asked: run,
            });
        }
        let used_by_others = self.power_used(outpost) - self.draw(installed);
        let free = self.power_cap.saturating_sub(used_by_others);
        let draw = tier(facility, run).map_or(0, |tier| tier.draw);
        if draw > free {
            return Err(Refusal::PowerShort {
                tier: run,
                draw,
                free,
            });
        }
        let mut next = outpost.clone();
        if let Some(installed) = next.installed_mut(socket.id) {
            installed.run = run;
        }
        Ok(Change {
            action: Action::Powered,
            outpost: next,
            cost: Cost::FREE,
            rig: None,
        })
    }

    /// The highest tier up to `owned` that the power left by `others` allows.
    fn best_run(&self, others: &Outpost, facility: &FacilityRule, owned: u32) -> u32 {
        let free = self.power_cap.saturating_sub(self.power_used(others));
        (1..=owned)
            .rev()
            .find(|&run| tier(facility, run).is_some_and(|tier| tier.draw <= free))
            .unwrap_or(0)
    }
}

/// Tier `number` of `facility`, counting from 1.
fn tier(facility: &FacilityRule, number: u32) -> Option<&'static TierRule> {
    let index = usize::try_from(number.checked_sub(1)?).ok()?;
    facility.tiers.get(index)
}

/// The tiers a facility running at `run` benefits from: that one and those
/// below it.
fn running(facility: &'static FacilityRule, run: u32) -> impl Iterator<Item = &'static TierRule> {
    let count = usize::try_from(run).unwrap_or(usize::MAX);
    facility.tiers.iter().take(count)
}

#[cfg(test)]
mod tests {
    use super::*;

    const RULES: OutpostRules = STAGE1;

    fn build(outpost: &Outpost, socket: &str, facility: &str) -> Outpost {
        RULES.install(outpost, socket, facility).unwrap().outpost
    }

    #[test]
    fn a_starter_outpost_has_one_empty_socket_and_the_core_processor() {
        let home = Outpost::default();
        let open: Vec<&str> = RULES
            .sockets
            .iter()
            .filter(|socket| RULES.is_open(&home, socket))
            .map(|socket| socket.id)
            .collect();
        assert_eq!(open, ["a"]);
        assert!(RULES.capabilities(&home).is_empty());
        assert_eq!(RULES.processor_batches(&home), 4);
        assert_eq!(RULES.open_tiers(&home), 1);
        assert_eq!(
            RULES.install(&home, "b", "refinery"),
            Err(Refusal::SocketClosed("Socket B"))
        );
    }

    #[test]
    fn the_first_facility_costs_its_price_brings_its_rig_and_opens_socket_b() {
        let change = RULES
            .install(&Outpost::default(), "a", "sensor_array")
            .unwrap();
        assert_eq!(change.action, Action::Bought);
        assert_eq!(change.cost.credits, 150);
        assert_eq!(change.cost.goods, &[(Good::Ore, 10)]);
        assert_eq!(change.rig, Some("scout_rig"));
        let home = change.outpost;
        assert_eq!(
            RULES.capabilities(&home),
            [Capability::LocateSignal, Capability::ScoutRig]
        );
        assert!(RULES.is_open(&home, RULES.socket("b").unwrap()));
        assert_eq!(RULES.power_used(&home), 2);
    }

    #[test]
    fn a_socket_takes_one_facility_and_a_facility_one_socket() {
        let home = build(&Outpost::default(), "a", "refinery");
        assert_eq!(
            RULES.install(&home, "a", "sensor_array"),
            Err(Refusal::SocketTaken("Socket A"))
        );
        assert_eq!(
            RULES.install(&home, "b", "refinery"),
            Err(Refusal::AlreadyInstalled("Refinery"))
        );
        assert!(matches!(
            RULES.install(&home, "c", "refinery"),
            Err(Refusal::NoSuchSocket(_))
        ));
        assert!(matches!(
            RULES.install(&home, "b", "shipyard"),
            Err(Refusal::NoSuchFacility(_))
        ));
    }

    #[test]
    fn power_holds_the_layouts_of_design_section_10() {
        // Two tier 2 facilities draw 6, over the cap of 5: the second one
        // upgraded runs at tier 1.
        let mut home = build(&Outpost::default(), "a", "refinery");
        home = build(&home, "b", "fabrication_bay");
        assert_eq!(RULES.power_used(&home), 4, "two tier 1 facilities");
        home = RULES.upgrade(&home, "a").unwrap().outpost;
        assert_eq!(RULES.power_used(&home), 5, "one tier 2 and one tier 1");
        home = RULES.upgrade(&home, "b").unwrap().outpost;
        assert_eq!(home.installed("b").unwrap().run, 1);
        assert_eq!(RULES.power_used(&home), 5);
        assert_eq!(
            RULES.power(&home, "b", 2),
            Err(Refusal::PowerShort {
                tier: 2,
                draw: 3,
                free: 2
            })
        );
        // Dropping the refinery to tier 1 frees the power for the other.
        home = RULES.power(&home, "a", 1).unwrap().outpost;
        home = RULES.power(&home, "b", 2).unwrap().outpost;
        assert_eq!(RULES.power_used(&home), 5);
        assert!(
            RULES
                .capabilities(&home)
                .contains(&Capability::PointDefence)
        );
        assert!(!RULES.capabilities(&home).contains(&Capability::HeatSink));
    }

    #[test]
    fn a_tier_2_facility_run_at_tier_1_grants_tier_1_only() {
        let mut home = build(&Outpost::default(), "a", "sensor_array");
        home = RULES.upgrade(&home, "a").unwrap().outpost;
        assert!(RULES.capabilities(&home).contains(&Capability::DeepScan));
        home = RULES.power(&home, "a", 1).unwrap().outpost;
        assert_eq!(
            RULES.capabilities(&home),
            [Capability::LocateSignal, Capability::ScoutRig]
        );
        assert_eq!(
            RULES.power(&home, "a", 3),
            Err(Refusal::TierNotOwned {
                facility: "Sensor array",
                owned: 2,
                asked: 3
            })
        );
    }

    #[test]
    fn an_unpowered_facility_grants_nothing() {
        let mut home = build(&Outpost::default(), "a", "refinery");
        home = RULES.power(&home, "a", 0).unwrap().outpost;
        assert!(RULES.capabilities(&home).is_empty());
        assert_eq!(RULES.processor_batches(&home), 4);
        assert_eq!(RULES.power_used(&home), 0);
    }

    #[test]
    fn the_refinery_runs_the_processor_and_works_harder_beside_the_cargo_bay() {
        let in_a = build(&Outpost::default(), "a", "refinery");
        assert_eq!(RULES.processor_batches(&in_a), 10, "8, and 2 for socket A");
        let in_a = RULES.upgrade(&in_a, "a").unwrap().outpost;
        assert_eq!(RULES.processor_batches(&in_a), 16);

        let mut in_b = build(&Outpost::default(), "a", "sensor_array");
        in_b = build(&in_b, "b", "refinery");
        assert_eq!(RULES.processor_batches(&in_b), 8);
    }

    #[test]
    fn a_fabrication_bay_beside_the_berth_cuts_fitting_prices() {
        let mut home = build(&Outpost::default(), "a", "fabrication_bay");
        assert_eq!(RULES.fitting_discount(&home), 0);
        home = RULES.remove(&home, "a").unwrap().outpost;
        home = RULES
            .install(&home, "b", "fabrication_bay")
            .unwrap()
            .outpost;
        assert_eq!(RULES.fitting_discount(&home), 20);
        home = RULES.power(&home, "b", 0).unwrap().outpost;
        assert_eq!(RULES.fitting_discount(&home), 0, "unpowered");
    }

    #[test]
    fn removal_is_free_and_installing_again_costs_the_reinstallation() {
        let home = build(&Outpost::default(), "a", "refinery");
        let removed = RULES.remove(&home, "a").unwrap();
        assert!(removed.cost.is_free());
        assert!(RULES.capabilities(&removed.outpost).is_empty());
        assert_eq!(removed.outpost.owned("refinery"), 1, "the unlock stays");
        assert!(
            RULES.is_open(&removed.outpost, RULES.socket("b").unwrap()),
            "socket B stays open"
        );
        let again = RULES.install(&removed.outpost, "b", "refinery").unwrap();
        assert_eq!(again.action, Action::Reinstalled);
        assert_eq!(again.cost, RULES.reinstall);
        assert_eq!(again.rig, None, "the first rig came with the purchase");
        assert_eq!(
            RULES.remove(&again.outpost, "a"),
            Err(Refusal::SocketEmpty("Socket A"))
        );
    }

    #[test]
    fn a_facility_reinstalled_after_its_upgrade_keeps_its_tier() {
        let mut home = build(&Outpost::default(), "a", "sensor_array");
        home = RULES.upgrade(&home, "a").unwrap().outpost;
        home = RULES.remove(&home, "a").unwrap().outpost;
        home = build(&home, "b", "sensor_array");
        assert_eq!(home.installed("b").unwrap().run, 2);
        assert_eq!(
            RULES.upgrade(&home, "b"),
            Err(Refusal::TopTier("Sensor array"))
        );
    }

    #[test]
    fn upgrades_cost_the_tier_2_price_and_open_threat_tiers_by_unlocks() {
        let mut home = build(&Outpost::default(), "a", "refinery");
        let upgrade = RULES.upgrade(&home, "a").unwrap();
        assert_eq!(upgrade.cost.credits, 550);
        assert_eq!(
            upgrade.cost.goods,
            &[(Good::Alloy, 12), (Good::Components, 5)]
        );
        home = upgrade.outpost;
        assert_eq!(RULES.open_tiers(&home), 2);
        home = build(&home, "b", "sensor_array");
        home = RULES.upgrade(&home, "b").unwrap().outpost;
        assert_eq!(RULES.open_tiers(&home), 3);
        // Tiers follow unlocks: removing both facilities keeps them open.
        home = RULES.remove(&home, "a").unwrap().outpost;
        home = RULES.remove(&home, "b").unwrap().outpost;
        assert_eq!(RULES.open_tiers(&home), 3);
        assert_eq!(
            RULES.upgrade(&home, "a"),
            Err(Refusal::SocketEmpty("Socket A"))
        );
    }

    #[test]
    fn a_new_facility_needs_only_data() {
        // A fourth facility, a socket and an adjacency rule, added as data:
        // nothing in the code knows about them.
        const SHIELD_WORKS: FacilityRule = FacilityRule {
            id: "shield_works",
            name: "Shield works",
            tiers: &[TierRule {
                cost: Cost::credits(90),
                draw: 1,
                grants: &[Capability::PointDefence],
                processor_batches: Some(6),
            }],
            rig: None,
        };
        const RULES: OutpostRules = OutpostRules {
            facilities: &[SHIELD_WORKS],
            sockets: &[SocketRule {
                id: "c",
                name: "Socket C",
                beside: "mast",
                opens: SocketOpens::AtStart,
            }],
            adjacency: &[AdjacencyRule {
                facility: "shield_works",
                socket: "c",
                bonus: Bonus::ProcessorBatches(1),
            }],
            ..STAGE1
        };
        let change = RULES
            .install(&Outpost::default(), "c", "shield_works")
            .unwrap();
        assert_eq!(change.cost.credits, 90);
        assert_eq!(change.rig, None);
        assert_eq!(
            RULES.capabilities(&change.outpost),
            [Capability::PointDefence]
        );
        assert_eq!(RULES.processor_batches(&change.outpost), 7);
        assert_eq!(RULES.power_used(&change.outpost), 1);
    }

    #[test]
    fn every_facility_opens_an_expedition_option_at_every_tier() {
        // Design section 2: none is only a rate.
        for facility in STAGE1.facilities {
            for (index, tier) in facility.tiers.iter().enumerate() {
                assert!(
                    !tier.grants.is_empty(),
                    "{} tier {} opens nothing",
                    facility.name,
                    index + 1
                );
            }
        }
    }
}
