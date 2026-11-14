//! Rigs, modules and the hangar (design sections 3, 6 and 10).
//!
//! The hull carries one rig and up to two modules; with no rig it flies the
//! basic fitting, which is always available and can never be lost. A rig or
//! module is fitted only while the capability it is keyed to is granted by
//! the powered layout. Each piece a home owns is an instance with its own
//! identity and a wear field, which Stage 1 keeps at zero.
//!
//! A home keeps a saved fitting by kind, not by instance: when a fitted piece
//! is lost, the saved fitting still names it, and the launch screen offers it
//! again at its replacement price. Every number here is a placeholder from
//! design section 10, or chosen where the design gives only words.

use serde::{Deserialize, Serialize};

use crate::site::{Capability, Material};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Slot {
    Rig,
    Module,
}

/// What a rig or module changes. Rates replace the basic rate of the pocket
/// that yields the material; the rich pocket still multiplies them.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Effect {
    /// A rig's hold, replacing the basic fitting's.
    pub capacity: Option<u32>,
    /// Added to the hold by a module.
    pub extra_capacity: u32,
    /// Units per minute.
    pub rates: &'static [(Material, f32)],
    /// Multiplies how long a scan takes.
    pub scan_time: f32,
    /// Multiplies top speed and acceleration.
    pub speed: f32,
    /// Multiplies weapon damage.
    pub weapon: f32,
    /// Hit points absorbed before the hull, regenerating out of combat.
    pub shield: f32,
    /// Multiplies how much extraction builds full heat.
    pub heat_capacity: f32,
    /// Intercepts projectiles near the ship.
    pub point_defence: bool,
    /// Scans from the approach leg, before reaching the site.
    pub deep_scan: bool,
}

const NO_EFFECT: Effect = Effect {
    capacity: None,
    extra_capacity: 0,
    rates: &[],
    scan_time: 1.0,
    speed: 1.0,
    weapon: 1.0,
    shield: 0.0,
    heat_capacity: 1.0,
    point_defence: false,
    deep_scan: false,
};

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct FittingRule {
    pub id: &'static str,
    pub name: &'static str,
    pub slot: Slot,
    /// The capability the powered layout must grant to fit it.
    pub needs: Capability,
    /// The NPC price, to buy it and to replace it.
    pub price: u32,
    pub effect: Effect,
}

/// The basic fitting's hold.
pub const BASIC_CAPACITY: u32 = crate::extraction::BASIC_CAPACITY;
/// Modules one hull carries.
pub const MODULE_SLOTS: usize = 2;

pub const FITTINGS: [FittingRule; 7] = [
    FittingRule {
        id: "scout_rig",
        name: "Scout rig",
        slot: Slot::Rig,
        needs: Capability::ScoutRig,
        price: 60,
        effect: Effect {
            capacity: Some(16),
            rates: &[(Material::Salvage, 1.6)],
            // "Fast scan": half the time.
            scan_time: 0.5,
            speed: 1.2,
            ..NO_EFFECT
        },
    },
    FittingRule {
        id: "extraction_rig",
        name: "Extraction rig",
        slot: Slot::Rig,
        needs: Capability::ExtractionRig,
        price: 60,
        effect: Effect {
            capacity: Some(32),
            rates: &[(Material::Ore, 3.0), (Material::Volatiles, 1.4)],
            ..NO_EFFECT
        },
    },
    FittingRule {
        id: "escort_rig",
        name: "Escort rig",
        slot: Slot::Rig,
        needs: Capability::EscortRig,
        price: 60,
        effect: Effect {
            capacity: Some(12),
            weapon: 2.0,
            shield: 40.0,
            ..NO_EFFECT
        },
    },
    FittingRule {
        id: "cargo_frame",
        name: "Cargo frame",
        slot: Slot::Module,
        needs: Capability::CargoFrame,
        price: 30,
        effect: Effect {
            extra_capacity: 6,
            ..NO_EFFECT
        },
    },
    FittingRule {
        id: "deep_scan",
        name: "Deep-scan array",
        slot: Slot::Module,
        needs: Capability::DeepScan,
        price: 90,
        effect: Effect {
            deep_scan: true,
            ..NO_EFFECT
        },
    },
    FittingRule {
        id: "heat_sink",
        name: "Heat sink",
        slot: Slot::Module,
        needs: Capability::HeatSink,
        price: 90,
        effect: Effect {
            heat_capacity: 1.25,
            ..NO_EFFECT
        },
    },
    FittingRule {
        id: "point_defence",
        name: "Point defence",
        slot: Slot::Module,
        needs: Capability::PointDefence,
        price: 90,
        effect: Effect {
            point_defence: true,
            ..NO_EFFECT
        },
    },
];

/// The rig or module called `id`.
#[must_use]
pub fn fitting(id: &str) -> Option<&'static FittingRule> {
    FITTINGS.iter().find(|fitting| fitting.id == id)
}

/// `price` less a percentage discount, rounded up so a discount never makes
/// something free.
#[must_use]
pub fn discounted(price: u32, percent: u32) -> u32 {
    let percent = percent.min(100);
    (price * (100 - percent)).div_ceil(100)
}

/// What a ship flies with: the basic fitting changed by its rig and modules.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Loadout {
    pub capacity: u32,
    /// Units per minute by material, where a fitting sets one.
    pub rates: [Option<f32>; 3],
    pub scan_time: f32,
    pub speed: f32,
    pub weapon: f32,
    pub shield: f32,
    pub heat_capacity: f32,
    pub point_defence: bool,
    pub deep_scan: bool,
}

impl Loadout {
    pub const BASIC: Self = Self {
        capacity: BASIC_CAPACITY,
        rates: [None; 3],
        scan_time: 1.0,
        speed: 1.0,
        weapon: 1.0,
        shield: 0.0,
        heat_capacity: 1.0,
        point_defence: false,
        deep_scan: false,
    };

    /// The basic fitting with `fittings` on it.
    #[must_use]
    pub fn of<'a>(fittings: impl IntoIterator<Item = &'a FittingRule>) -> Self {
        let mut loadout = Self::BASIC;
        let mut extra = 0;
        for fitting in fittings {
            let effect = &fitting.effect;
            if let Some(capacity) = effect.capacity {
                loadout.capacity = capacity;
            }
            extra += effect.extra_capacity;
            for &(material, rate) in effect.rates {
                loadout.rates[material.index()] = Some(rate);
            }
            loadout.scan_time *= effect.scan_time;
            loadout.speed *= effect.speed;
            loadout.weapon *= effect.weapon;
            loadout.shield += effect.shield;
            loadout.heat_capacity *= effect.heat_capacity;
            loadout.point_defence |= effect.point_defence;
            loadout.deep_scan |= effect.deep_scan;
        }
        loadout.capacity += extra;
        loadout
    }

    /// The loadout of the fittings called `ids`; unknown names are ignored.
    #[must_use]
    pub fn of_ids<'a>(ids: impl IntoIterator<Item = &'a str>) -> Self {
        Self::of(ids.into_iter().filter_map(fitting))
    }

    /// Units per minute of `material`, given the pocket's basic rate.
    #[must_use]
    pub fn rate(&self, material: Material, basic: f32) -> f32 {
        self.rates[material.index()].unwrap_or(basic)
    }
}

/// One rig or module a home owns.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Piece {
    pub id: u32,
    pub fitting: String,
    /// In the data model from Stage 1, switched off (design section 7).
    pub wear: u32,
}

/// The fitting a pilot saved, by kind.
#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct Saved {
    pub rig: Option<String>,
    pub modules: Vec<String>,
}

/// Every rig and module a home owns, and the fitting it flies.
#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct Hangar {
    pub pieces: Vec<Piece>,
    pub next_id: u32,
    pub saved: Saved,
}

/// Why a saved piece does not fly.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Missing {
    /// None is in the hangar: it was lost, or never bought.
    NotOwned,
    /// The powered layout does not grant what it needs.
    NeedsCapability(Capability),
}

/// What a launch carries: the pieces flown, and the saved ones left behind.
#[derive(Debug, Clone, PartialEq)]
pub struct Launch {
    pub flown: Vec<Piece>,
    pub missing: Vec<(&'static FittingRule, Missing)>,
}

impl Launch {
    #[must_use]
    pub fn loadout(&self) -> Loadout {
        Loadout::of_ids(self.flown.iter().map(|piece| piece.fitting.as_str()))
    }

    /// What the pilot stands to lose: each piece flown at its replacement
    /// price (design section 6).
    #[must_use]
    pub fn stake(&self, discount: u32) -> u32 {
        self.flown
            .iter()
            .filter_map(|piece| fitting(&piece.fitting))
            .map(|fitting| discounted(fitting.price, discount))
            .sum()
    }
}

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum FitRefusal {
    #[error("there is no fitting {0}")]
    NoSuchFitting(String),
    #[error("the {0} is a module, not a rig")]
    NotARig(&'static str),
    #[error("the {0} is a rig, not a module")]
    NotAModule(&'static str),
    #[error("a hull carries at most {MODULE_SLOTS} modules")]
    TooManyModules,
    #[error("one {0} is enough")]
    Twice(&'static str),
    #[error("the {name} needs the {needs}")]
    NeedsCapability {
        name: &'static str,
        needs: &'static str,
    },
}

impl Hangar {
    /// Adds a new piece of `fitting` and returns its identifier.
    pub fn add(&mut self, fitting: &FittingRule) -> u32 {
        let id = self.next_id;
        self.next_id += 1;
        self.pieces.push(Piece {
            id,
            fitting: fitting.id.to_owned(),
            wear: 0,
        });
        id
    }

    /// Pieces of `fitting` owned.
    #[must_use]
    pub fn count(&self, fitting: &str) -> usize {
        self.pieces
            .iter()
            .filter(|piece| piece.fitting == fitting)
            .count()
    }

    /// Checks that a home granted `capabilities` may buy `fitting`.
    ///
    /// # Errors
    /// Fails if there is no such fitting or its capability is not granted.
    pub fn may_buy(
        fitting: &str,
        capabilities: &[Capability],
    ) -> Result<&'static FittingRule, FitRefusal> {
        let rule =
            self::fitting(fitting).ok_or_else(|| FitRefusal::NoSuchFitting(fitting.into()))?;
        if !capabilities.contains(&rule.needs) {
            return Err(FitRefusal::NeedsCapability {
                name: rule.name,
                needs: rule.needs.name(),
            });
        }
        Ok(rule)
    }

    /// Saves the fitting to fly. Pieces need not be owned or granted yet: the
    /// launch screen says what is missing and what it costs.
    ///
    /// # Errors
    /// Fails if a name is unknown, in the wrong slot, repeated, or there are
    /// more modules than slots.
    pub fn save(&mut self, rig: Option<&str>, modules: &[&str]) -> Result<(), FitRefusal> {
        if let Some(rig) = rig {
            let rule = fitting(rig).ok_or_else(|| FitRefusal::NoSuchFitting(rig.into()))?;
            if rule.slot != Slot::Rig {
                return Err(FitRefusal::NotARig(rule.name));
            }
        }
        if modules.len() > MODULE_SLOTS {
            return Err(FitRefusal::TooManyModules);
        }
        for (index, module) in modules.iter().enumerate() {
            let rule =
                fitting(module).ok_or_else(|| FitRefusal::NoSuchFitting((*module).into()))?;
            if rule.slot != Slot::Module {
                return Err(FitRefusal::NotAModule(rule.name));
            }
            // Two of one module would let a hull outgrow the stake design
            // section 10 prices as the most expensive fitting.
            if modules[..index].contains(module) {
                return Err(FitRefusal::Twice(rule.name));
            }
        }
        self.saved = Saved {
            rig: rig.map(str::to_owned),
            modules: modules.iter().map(|module| (*module).to_owned()).collect(),
        };
        Ok(())
    }

    /// What launching now would carry, given the layout's capabilities.
    #[must_use]
    pub fn launch(&self, capabilities: &[Capability]) -> Launch {
        let mut flown: Vec<Piece> = Vec::new();
        let mut missing = Vec::new();
        for name in self.saved.rig.iter().chain(&self.saved.modules) {
            let Some(rule) = fitting(name) else {
                continue;
            };
            if !capabilities.contains(&rule.needs) {
                missing.push((rule, Missing::NeedsCapability(rule.needs)));
                continue;
            }
            match self
                .pieces
                .iter()
                .find(|piece| piece.fitting == *name && !flown.contains(piece))
            {
                Some(piece) => flown.push(piece.clone()),
                None => missing.push((rule, Missing::NotOwned)),
            }
        }
        Launch { flown, missing }
    }

    /// Removes pieces destroyed with the ship (design section 6). The saved
    /// fitting still names them.
    pub fn lose(&mut self, lost: &[u32]) {
        self.pieces.retain(|piece| !lost.contains(&piece.id));
    }

    /// Removes one piece of each fitting named in `kinds`: what a ship flew,
    /// as a run records it. Returns how many were removed.
    pub fn lose_kinds(&mut self, kinds: &[String]) -> usize {
        let mut removed = 0;
        for kind in kinds {
            if let Some(index) = self.pieces.iter().position(|piece| piece.fitting == *kind) {
                self.pieces.remove(index);
                removed += 1;
            }
        }
        removed
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rule(id: &str) -> &'static FittingRule {
        fitting(id).unwrap()
    }

    const EVERYTHING: [Capability; 7] = [
        Capability::ScoutRig,
        Capability::ExtractionRig,
        Capability::EscortRig,
        Capability::CargoFrame,
        Capability::DeepScan,
        Capability::HeatSink,
        Capability::PointDefence,
    ];

    #[test]
    fn the_basic_fitting_holds_20_at_basic_rates() {
        let basic = Loadout::of([]);
        assert_eq!(basic, Loadout::BASIC);
        assert_eq!(basic.capacity, 20);
        assert!((basic.rate(Material::Ore, 2.0) - 2.0).abs() < f32::EPSILON);
    }

    #[test]
    fn rigs_and_modules_have_the_effects_of_design_section_10() {
        let extraction = Loadout::of([rule("extraction_rig"), rule("cargo_frame")]);
        assert_eq!(extraction.capacity, 38, "32 and the cargo frame's 6");
        assert!((extraction.rate(Material::Ore, 2.0) - 3.0).abs() < f32::EPSILON);
        assert!((extraction.rate(Material::Volatiles, 0.8) - 1.4).abs() < f32::EPSILON);
        assert!((extraction.rate(Material::Salvage, 1.2) - 1.2).abs() < f32::EPSILON);

        let scout = Loadout::of([rule("scout_rig"), rule("deep_scan")]);
        assert_eq!(scout.capacity, 16);
        assert!((scout.rate(Material::Salvage, 1.2) - 1.6).abs() < f32::EPSILON);
        assert!(scout.scan_time < 1.0 && scout.deep_scan);
        assert!((scout.speed - 1.2).abs() < f32::EPSILON, "20% faster");

        let escort = Loadout::of([rule("escort_rig"), rule("point_defence")]);
        assert_eq!(escort.capacity, 12);
        assert!((escort.weapon - 2.0).abs() < f32::EPSILON, "double output");
        assert!(escort.shield > 0.0 && escort.point_defence);

        let sink = Loadout::of([rule("heat_sink")]);
        assert!((sink.heat_capacity - 1.25).abs() < f32::EPSILON);
    }

    #[test]
    fn the_stake_of_design_section_10_is_180_cr_of_modules() {
        let mut hangar = Hangar::default();
        for id in ["extraction_rig", "heat_sink", "cargo_frame"] {
            hangar.add(rule(id));
        }
        hangar
            .save(Some("extraction_rig"), &["heat_sink", "cargo_frame"])
            .unwrap();
        let launch = hangar.launch(&EVERYTHING);
        assert!(launch.missing.is_empty());
        assert_eq!(launch.stake(0), 180);
        assert_eq!(launch.stake(20), 144, "a fabrication bay beside the berth");
    }

    #[test]
    fn a_piece_flies_only_when_owned_and_granted() {
        let mut hangar = Hangar::default();
        hangar.add(rule("scout_rig"));
        hangar.save(Some("scout_rig"), &["cargo_frame"]).unwrap();
        let launch = hangar.launch(&[Capability::ScoutRig]);
        assert_eq!(launch.flown.len(), 1);
        assert_eq!(
            launch.missing,
            [(
                rule("cargo_frame"),
                Missing::NeedsCapability(Capability::CargoFrame)
            )]
        );
        let launch = hangar.launch(&[Capability::ScoutRig, Capability::CargoFrame]);
        assert_eq!(launch.missing, [(rule("cargo_frame"), Missing::NotOwned)]);
        // Unpowered, the rig stays home: the basic fitting flies.
        let launch = hangar.launch(&[]);
        assert!(launch.flown.is_empty());
        assert_eq!(launch.loadout(), Loadout::BASIC);
    }

    #[test]
    fn a_lost_piece_stays_in_the_saved_fitting_to_be_bought_again() {
        let mut hangar = Hangar::default();
        let rig = hangar.add(rule("escort_rig"));
        hangar.save(Some("escort_rig"), &[]).unwrap();
        let flown = hangar.launch(&EVERYTHING).flown;
        assert_eq!(flown[0].id, rig);
        hangar.lose(&[rig]);
        assert_eq!(hangar.count("escort_rig"), 0);
        assert_eq!(hangar.saved.rig.as_deref(), Some("escort_rig"));
        let launch = hangar.launch(&EVERYTHING);
        assert_eq!(launch.missing, [(rule("escort_rig"), Missing::NotOwned)]);
        assert_eq!(hangar.add(rule("escort_rig")), rig + 1, "a new identity");
    }

    #[test]
    fn losing_by_kind_takes_one_piece_of_each() {
        let mut hangar = Hangar::default();
        hangar.add(rule("heat_sink"));
        hangar.add(rule("heat_sink"));
        hangar.add(rule("extraction_rig"));
        let lost = hangar.lose_kinds(&["heat_sink".into(), "extraction_rig".into(), "warp".into()]);
        assert_eq!(lost, 2);
        assert_eq!(hangar.count("heat_sink"), 1, "the spare stays");
        assert_eq!(hangar.count("extraction_rig"), 0);
    }

    #[test]
    fn a_saved_fitting_has_one_rig_and_two_different_modules() {
        let mut hangar = Hangar::default();
        assert_eq!(
            hangar.save(Some("cargo_frame"), &[]),
            Err(FitRefusal::NotARig("Cargo frame"))
        );
        assert_eq!(
            hangar.save(None, &["scout_rig"]),
            Err(FitRefusal::NotAModule("Scout rig"))
        );
        assert_eq!(
            hangar.save(None, &["heat_sink", "heat_sink"]),
            Err(FitRefusal::Twice("Heat sink"))
        );
        assert_eq!(
            hangar.save(None, &["heat_sink", "cargo_frame", "deep_scan"]),
            Err(FitRefusal::TooManyModules)
        );
        assert!(matches!(
            hangar.save(Some("warp_core"), &[]),
            Err(FitRefusal::NoSuchFitting(_))
        ));
        assert_eq!(hangar.saved, Saved::default(), "refusals change nothing");
    }

    #[test]
    fn buying_needs_the_capability_and_discounts_round_up() {
        assert_eq!(
            Hangar::may_buy("heat_sink", &[Capability::CargoFrame]),
            Err(FitRefusal::NeedsCapability {
                name: "Heat sink",
                needs: "Heat sink module"
            })
        );
        assert!(Hangar::may_buy("heat_sink", &[Capability::HeatSink]).is_ok());
        assert_eq!(discounted(60, 20), 48);
        assert_eq!(discounted(30, 20), 24);
        assert_eq!(discounted(1, 99), 1);
        assert_eq!(discounted(90, 100), 0);
    }

    #[test]
    fn every_fitting_is_keyed_to_a_capability_some_facility_grants() {
        let granted: Vec<Capability> = crate::outpost::STAGE1
            .facilities
            .iter()
            .flat_map(|facility| facility.tiers.iter())
            .flat_map(|tier| tier.grants.iter().copied())
            .collect();
        for fitting in FITTINGS {
            assert!(granted.contains(&fitting.needs), "{}", fitting.name);
        }
        for facility in crate::outpost::STAGE1.facilities {
            let Some(id) = facility.rig else { continue };
            let rig = self::fitting(id).unwrap();
            assert_eq!(rig.slot, Slot::Rig);
            assert!(facility.tiers[0].grants.contains(&rig.needs));
        }
    }
}
