//! The gate 1 rules check (design section 9, plan 7.4): does a facility
//! change the best expedition choice, beyond opening new ones?
//!
//! For one seed and two layouts, the best choice under each is the pocket and
//! rig with the best per-run take among the options open under both, so a
//! facility's unlocks alone never count; only what it does to options that
//! were open either way.
//!
//! The take is a model, chosen by the owner on 14 November 2026. A solo pilot
//! at threat tier I fills the hold from one pocket, with the modules that add
//! to it, and the haul is valued at the better of selling it raw and
//! processing it first. The processor counts only as far as it gets in one
//! run's length, so the pace a layout gives it is the one way a facility
//! changes the take of an option already open. The nest core pays its bounty
//! and the salvage it drops. Time and risk are left out: the scout rig's
//! speed and scan, the escort rig's guns and shield, heat and the fitting
//! discount count for nothing, and the contract payout and launch cost, the
//! same for every choice, are not added.

use crate::fitting::{FITTINGS, FittingRule, Loadout, MODULE_SLOTS, Slot};
use crate::goods::Good;
use crate::market::{RECIPES, price};
use crate::outpost::{Installed, Outpost, OutpostRules, Unlock};
use crate::run::{NEST_BOUNTY, NEST_SALVAGE};
use crate::site::{Capability, PocketKind, Site, SiteTemplate, ThreatTier};

/// How long the processor works on one run's haul before the next comes in:
/// design section 10's runs of about 30 minutes.
pub const RUN_SECONDS: u32 = 30 * 60;

/// What a pilot chooses on a seed: the pocket worked and the rig flown.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Choice {
    pub pocket: PocketKind,
    /// `None` for the basic fitting.
    pub rig: Option<&'static str>,
}

impl Choice {
    /// The choice in words, such as "Ore field, extraction rig".
    #[must_use]
    pub fn name(&self) -> String {
        let pocket = self.pocket.name();
        let rig = self
            .rig
            .and_then(crate::fitting::fitting)
            .map_or("basic fitting".to_owned(), |rig| rig.name.to_lowercase());
        format!("{pocket}, {rig}")
    }
}

/// A choice and its take in credits.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Valued {
    pub choice: Choice,
    pub take: u32,
}

/// What a haul of `units` of `good` is worth when the processor takes
/// `batch_seconds` over a batch: as many batches as it finishes in a run,
/// where processing pays, and the rest sold raw.
#[must_use]
pub fn haul_value(good: Good, units: u32, batch_seconds: u32) -> u32 {
    let raw = price(good).buys_at;
    let Some(recipe) = RECIPES.iter().find(|recipe| recipe.input.0 == good) else {
        return units * raw;
    };
    let (_, per_batch) = recipe.input;
    let (output, made) = recipe.output;
    let batch_value = made * price(output).buys_at;
    if per_batch == 0 || batch_value <= per_batch * raw {
        return units * raw;
    }
    let batches = (units / per_batch).min(RUN_SECONDS / batch_seconds.max(1));
    batches * batch_value + (units - batches * per_batch) * raw
}

/// The take of working pocket `pocket` of `site` with `rig`, the modules open
/// under `open`, and the processor at `batch_seconds`.
#[must_use]
pub fn take(
    site: &Site,
    pocket: usize,
    rig: Option<&FittingRule>,
    open: &[Capability],
    batch_seconds: u32,
) -> u32 {
    let mut modules: Vec<&FittingRule> = FITTINGS
        .iter()
        .filter(|fitting| fitting.slot == Slot::Module && open.contains(&fitting.needs))
        .collect();
    // Only a module that adds to the hold changes the take.
    modules.sort_by_key(|module| std::cmp::Reverse(module.effect.extra_capacity));
    modules.truncate(MODULE_SLOTS);
    let hold = Loadout::of(rig.into_iter().chain(modules)).capacity;
    let Some(pocket) = site.pockets.get(pocket) else {
        return 0;
    };
    match pocket.template.material {
        Some(material) => {
            // A pool of at most a few hundred units, so `as` loses nothing.
            let units = hold.min(pocket.pool.floor() as u32);
            haul_value(Good::from_material(material), units, batch_seconds)
        }
        None => NEST_BOUNTY + haul_value(Good::Salvage, NEST_SALVAGE.min(hold), batch_seconds),
    }
}

/// The best choice on `site` among the pockets and rigs `open` allows, with
/// the processor at `batch_seconds`. Ties go to the earlier pocket, then to
/// the basic fitting and the earlier rig, so equal takes never read as a
/// change.
#[must_use]
pub fn best(site: &Site, open: &[Capability], batch_seconds: u32) -> Valued {
    let rigs: Vec<Option<&FittingRule>> = std::iter::once(None)
        .chain(
            FITTINGS
                .iter()
                .filter(|fitting| fitting.slot == Slot::Rig && open.contains(&fitting.needs))
                .map(Some),
        )
        .collect();
    let mut best: Option<Valued> = None;
    for (index, pocket) in site.pockets.iter().enumerate() {
        if pocket
            .template
            .needs
            .is_some_and(|needs| !open.contains(&needs))
        {
            continue;
        }
        for rig in &rigs {
            let valued = Valued {
                choice: Choice {
                    pocket: pocket.template.kind,
                    rig: rig.map(|rig| rig.id),
                },
                take: take(site, index, *rig, open, batch_seconds),
            };
            if best.is_none_or(|best| valued.take > best.take) {
                best = Some(valued);
            }
        }
    }
    // The ore field needs nothing, so a site always has an open pocket.
    best.unwrap_or(Valued {
        choice: Choice {
            pocket: PocketKind::OreField,
            rig: None,
        },
        take: 0,
    })
}

/// Every layout of the facilities: each socket empty or holding a facility
/// running at one of its tiers, no facility twice, within the power cap.
/// Every facility is owned at its top tier, since unlocks only decide which
/// layouts a home can reach, not what a layout does. An unpowered facility
/// does what an empty socket does, so none is listed.
#[must_use]
pub fn layouts(rules: &OutpostRules) -> Vec<Outpost> {
    let unlocks: Vec<Unlock> = rules
        .facilities
        .iter()
        .map(|facility| Unlock {
            facility: facility.id.to_owned(),
            tier: u32::try_from(facility.tiers.len()).unwrap_or(u32::MAX),
        })
        .collect();
    let mut choices: Vec<Option<(&str, u32)>> = vec![None];
    for facility in rules.facilities {
        for tier in 1..=facility.tiers.len() {
            choices.push(Some((facility.id, u32::try_from(tier).unwrap_or(u32::MAX))));
        }
    }
    let mut layouts: Vec<Vec<Installed>> = vec![Vec::new()];
    for socket in rules.sockets {
        let mut next = Vec::new();
        for layout in &layouts {
            for choice in &choices {
                let mut layout = layout.clone();
                if let Some((facility, run)) = choice {
                    if layout
                        .iter()
                        .any(|installed| installed.facility == *facility)
                    {
                        continue;
                    }
                    layout.push(Installed {
                        socket: socket.id.to_owned(),
                        facility: (*facility).to_owned(),
                        run: *run,
                    });
                }
                next.push(layout);
            }
        }
        layouts = next;
    }
    layouts
        .into_iter()
        .map(|layout| Outpost {
            unlocks: unlocks.clone(),
            layout,
        })
        .filter(|outpost| {
            rules.power_used(outpost) <= rules.power_cap
                && rules.sockets.iter().all(|socket| {
                    outpost.installed(socket.id).is_none() || rules.is_open(outpost, socket)
                })
        })
        .collect()
}

/// A layout in words, such as "A: refinery II · B: sensor array I".
#[must_use]
pub fn describe(rules: &OutpostRules, outpost: &Outpost) -> String {
    let placed: Vec<String> = rules
        .sockets
        .iter()
        .filter_map(|socket| {
            let installed = outpost.installed(socket.id)?;
            let name = rules
                .facility(&installed.facility)
                .map_or(installed.facility.as_str(), |facility| facility.name);
            let tier = if installed.run >= 2 { "II" } else { "I" };
            Some(format!(
                "{}: {} {tier}",
                socket.id.to_uppercase(),
                name.to_lowercase()
            ))
        })
        .collect();
    if placed.is_empty() {
        "empty".to_owned()
    } else {
        placed.join(" · ")
    }
}

/// Two layouts under which the best choice open to both differs.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Changed {
    pub first: Outpost,
    pub second: Outpost,
    pub under_first: Valued,
    pub under_second: Valued,
    /// How firmly the layouts disagree: each prefers its own best choice to
    /// the other's by some credits, and this is the smaller preference. A pilot
    /// nearly indifferent under one layout is not steered by it.
    pub gain: u32,
    /// The best choice's take under the layout with the smaller preference.
    pub of: u32,
}

impl Changed {
    /// The gain as a share of the take.
    #[must_use]
    pub fn gain_share(&self) -> f64 {
        if self.of == 0 {
            0.0
        } else {
            f64::from(self.gain) / f64::from(self.of)
        }
    }
}

/// The take of `choice` on `site`, with the modules `open` allows.
fn value(site: &Site, choice: Choice, open: &[Capability], batch_seconds: u32) -> u32 {
    let rig = choice.rig.and_then(crate::fitting::fitting);
    site.pockets
        .iter()
        .position(|pocket| pocket.template.kind == choice.pocket)
        .map_or(0, |pocket| take(site, pocket, rig, open, batch_seconds))
}

/// The check on one seed.
#[derive(Debug, Clone, PartialEq)]
pub struct SeedCheck {
    pub site: Site,
    /// The best choice under any layout, with what that layout opens.
    pub best_anywhere: Valued,
    pub layouts: usize,
    /// Pairs of distinct layouts compared.
    pub pairs: usize,
    pub changed: Vec<Changed>,
}

/// Runs the check on `seed` of `template` under `rules`.
#[must_use]
pub fn check(rules: &OutpostRules, template: SiteTemplate, seed: u32) -> SeedCheck {
    let site = Site::generate(template, seed, ThreatTier::I, 1, &Capability::ALL);
    let layouts = layouts(rules);
    let opened: Vec<(Vec<Capability>, u32)> = layouts
        .iter()
        .map(|outpost| (rules.capabilities(outpost), rules.batch_seconds(outpost)))
        .collect();
    let best_anywhere = opened
        .iter()
        .map(|(open, batch)| best(&site, open, *batch))
        .fold(None::<Valued>, |found, valued| match found {
            Some(found) if found.take >= valued.take => Some(found),
            _ => Some(valued),
        })
        .unwrap_or_else(|| best(&site, &[], rules.batch_seconds));
    let mut pairs = 0;
    let mut changed = Vec::new();
    for (i, (open_i, batch_i)) in opened.iter().enumerate() {
        for (j, (open_j, batch_j)) in opened.iter().enumerate().skip(i + 1) {
            pairs += 1;
            let both: Vec<Capability> = open_i
                .iter()
                .copied()
                .filter(|capability| open_j.contains(capability))
                .collect();
            let (under_first, under_second) =
                (best(&site, &both, *batch_i), best(&site, &both, *batch_j));
            if under_first.choice != under_second.choice {
                // Each best is the most its layout gives, so neither
                // difference goes below zero.
                let gains = [
                    (
                        under_first.take.saturating_sub(value(
                            &site,
                            under_second.choice,
                            &both,
                            *batch_i,
                        )),
                        under_first.take,
                    ),
                    (
                        under_second.take.saturating_sub(value(
                            &site,
                            under_first.choice,
                            &both,
                            *batch_j,
                        )),
                        under_second.take,
                    ),
                ];
                let (gain, of) = gains
                    .into_iter()
                    .min_by_key(|&(gain, _)| gain)
                    .unwrap_or_default();
                changed.push(Changed {
                    first: layouts[i].clone(),
                    second: layouts[j].clone(),
                    under_first,
                    under_second,
                    gain,
                    of,
                });
            }
        }
    }
    SeedCheck {
        site,
        best_anywhere,
        layouts: layouts.len(),
        pairs,
        changed,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::goods::Cost;
    use crate::outpost::{FacilityRule, STAGE1, SocketOpens, SocketRule, TierRule};
    use crate::site::FRONTIER_SITE;

    #[test]
    fn processing_counts_only_as_far_as_the_processor_gets_in_a_run() {
        // Ore: 2 become 1 alloy, 20 credits become 26. At the core's 240 s a
        // run's 1,800 s finishes 7 batches: 14 ore processed, 18 sold raw.
        assert_eq!(haul_value(Good::Ore, 32, 240), 7 * 26 + 18 * 10);
        // At a refinery's 60 s every batch is done.
        assert_eq!(haul_value(Good::Ore, 32, 60), 16 * 26);
        // An odd unit sells raw; volatiles have no recipe.
        assert_eq!(haul_value(Good::Salvage, 5, 60), 2 * 46 + 18);
        assert_eq!(haul_value(Good::Volatiles, 16, 60), 16 * 22);
    }

    #[test]
    fn the_hold_limits_the_take_and_a_cargo_frame_adds_to_it() {
        // Seed 16's rich pocket is the nest core: the ore field holds 40.
        let site = Site::generate(FRONTIER_SITE, 16, ThreatTier::I, 1, &Capability::ALL);
        let rig = crate::fitting::fitting("extraction_rig");
        let slow = 1_000_000;
        assert_eq!(take(&site, 0, None, &[], slow), 20 * 10);
        assert_eq!(take(&site, 0, rig, &[], slow), 32 * 10);
        assert_eq!(
            take(&site, 0, rig, &[Capability::CargoFrame], slow),
            38 * 10
        );
        // The nest core: its bounty and six salvage.
        assert_eq!(take(&site, 3, None, &[], slow), 120 + 6 * 18);
    }

    #[test]
    fn the_stage1_layouts_are_every_powered_pair_within_the_cap() {
        let layouts = layouts(&STAGE1);
        // Empty; one facility at one of two tiers in either socket (12); two
        // different facilities, at tiers I and I, I and II or II and I, in
        // either order (18). Two at tier II draw 6, over the cap of 5.
        assert_eq!(layouts.len(), 31);
        assert!(
            layouts
                .iter()
                .all(|outpost| STAGE1.power_used(outpost) <= STAGE1.power_cap)
        );
    }

    #[test]
    fn an_unlock_alone_does_not_count_as_a_change() {
        // Seed 8's rich pocket is the derelict: with a sensor array it is the
        // best choice, but compared with an empty outpost it is not open
        // under both, so the pair shows no change.
        let site = Site::generate(FRONTIER_SITE, 8, ThreatTier::I, 1, &Capability::ALL);
        let sensor = STAGE1.capabilities(&Outpost {
            unlocks: vec![],
            layout: vec![Installed {
                socket: "a".into(),
                facility: "sensor_array".into(),
                run: 1,
            }],
        });
        assert_eq!(
            best(&site, &sensor, STAGE1.batch_seconds).choice.pocket,
            PocketKind::Derelict
        );
        let check = check(&STAGE1, FRONTIER_SITE, 8);
        let empty_against_sensor = check
            .changed
            .iter()
            .any(|changed| changed.first.layout.is_empty() || changed.second.layout.is_empty());
        assert!(!empty_against_sensor);
    }

    /// Rules where a mill only speeds the processor, and the core barely
    /// works: the mill changes nothing a pilot can fly, only what a haul is
    /// worth.
    const LAB_AND_MILL: OutpostRules = OutpostRules {
        power_cap: 5,
        batch_seconds: 1_000_000,
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
                opens: SocketOpens::AtStart,
            },
        ],
        facilities: &[
            FacilityRule {
                id: "lab",
                name: "Lab",
                tiers: &[TierRule {
                    cost: Cost::FREE,
                    draw: 1,
                    grants: &[Capability::LocateSignal, Capability::ExtractionRig],
                    batch_seconds: None,
                }],
                rig: None,
            },
            FacilityRule {
                id: "mill",
                name: "Mill",
                tiers: &[TierRule {
                    cost: Cost::FREE,
                    draw: 1,
                    grants: &[],
                    batch_seconds: Some(60),
                }],
                rig: None,
            },
        ],
        adjacency: &[],
        reinstall: Cost::FREE,
        top_threat_tier: 1,
    };

    #[test]
    fn a_facility_that_changes_what_a_haul_is_worth_changes_the_best_choice() {
        // Plan 7.4's fixture, on seed 16 (rich nest core). With the lab alone
        // nothing is processed: 18 salvage sell for 324, beating 32 ore at
        // 320. Add the mill and every batch is done: 32 ore make 416, 18
        // salvage 414. The options are the same; the best one is not.
        let check = check(&LAB_AND_MILL, FRONTIER_SITE, 16);
        let lab =
            |outpost: &Outpost| outpost.layout.len() == 1 && outpost.layout[0].facility == "lab";
        let found = check
            .changed
            .iter()
            .find(|changed| {
                (lab(&changed.first) && changed.second.layout.len() == 2)
                    || (lab(&changed.second) && changed.first.layout.len() == 2)
            })
            .expect("the mill changes the best choice");
        let (with_lab, with_both) = if lab(&found.first) {
            (found.under_first, found.under_second)
        } else {
            (found.under_second, found.under_first)
        };
        // The derelict's 18 fit the basic hold of 20, so the tie goes to the
        // basic fitting.
        let rig = Some("extraction_rig");
        assert_eq!(
            with_lab,
            Valued {
                choice: Choice {
                    pocket: PocketKind::Derelict,
                    rig: None
                },
                take: 324
            }
        );
        assert_eq!(
            with_both,
            Valued {
                choice: Choice {
                    pocket: PocketKind::OreField,
                    rig
                },
                take: 416
            }
        );
        // The lab prefers the derelict by 4 credits; the mill, the ore field by 2.
        assert_eq!((found.gain, found.of), (2, 416));
    }

    #[test]
    fn on_the_playtest_pool_stage1_layouts_steer_the_best_choice_by_two_credits_at_most() {
        // The finding the gate 1 write-up starts from. With only a
        // fabrication bay open under both layouts, the nest core pays 120 and
        // 6 salvage (258) and the ore field fills the basic hold of 20: 242 at
        // the core's pace, 260 once a refinery's processor works every batch.
        // So a refinery tips the choice, but leaves its owner 2 credits from
        // indifferent; nothing else changes the best choice at all.
        for seed in [9, 10, 20, 16, 8, 3, 5] {
            let check = check(&STAGE1, FRONTIER_SITE, seed);
            assert_eq!((check.layouts, check.pairs), (31, 465));
            assert!(!check.changed.is_empty(), "seed {seed}");
            let largest = check
                .changed
                .iter()
                .map(|changed| changed.gain)
                .max()
                .unwrap_or(0);
            assert_eq!(largest, 2, "seed {seed}");
            assert!(check.changed.iter().all(|changed| {
                let pockets = [
                    changed.under_first.choice.pocket,
                    changed.under_second.choice.pocket,
                ];
                pockets.contains(&PocketKind::NestCore) && pockets.contains(&PocketKind::OreField)
            }));
        }
    }

    #[test]
    fn the_same_seed_gives_the_same_check() {
        assert_eq!(
            check(&STAGE1, FRONTIER_SITE, 9),
            check(&STAGE1, FRONTIER_SITE, 9)
        );
    }
}
