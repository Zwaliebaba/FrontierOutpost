//! Translations from the simulation's types to the wire's. `frontier-sim`
//! knows nothing of the protocol, so the server maps one to the other.

use frontier_protocol::{
    AccountInfo, AdjacencyInfo, Catalog, CostInfo, DominantThreat, Ending, FacilityInfo,
    FittingInfo, Good, GoodAmount, Hazard, LaunchInfo, Material, MissingInfo, OutpostInfo,
    PocketInfo, PocketKind, PriceInfo, QueuedInfo, RecipeInfo, RunSummary, ServerMessage, SiteInfo,
    Slot, SocketRuleInfo, SocketState, TierInfo, TierOffer, UnlockInfo,
};
use frontier_sim::fitting::{self, FITTINGS, Missing};
use frontier_sim::market::{PRICES, RECIPES};
use frontier_sim::outpost::{Bonus, STAGE1};
use frontier_sim::run::{self, DOCK_RADIUS, LAUNCH_COST, seconds};
use frontier_sim::site::{self, Capability, Site, SiteTemplate, ThreatTier};
use frontier_sim::{goods, outcome};

use crate::account::Account;
use crate::home;

#[must_use]
pub const fn good(good: goods::Good) -> Good {
    match good {
        goods::Good::Ore => Good::Ore,
        goods::Good::Salvage => Good::Salvage,
        goods::Good::Volatiles => Good::Volatiles,
        goods::Good::Alloy => Good::Alloy,
        goods::Good::Components => Good::Components,
    }
}

#[must_use]
pub const fn from_good(good: Good) -> goods::Good {
    match good {
        Good::Ore => goods::Good::Ore,
        Good::Salvage => goods::Good::Salvage,
        Good::Volatiles => goods::Good::Volatiles,
        Good::Alloy => goods::Good::Alloy,
        Good::Components => goods::Good::Components,
    }
}

fn cost(cost: &goods::Cost) -> CostInfo {
    CostInfo {
        credits: cost.credits,
        goods: cost
            .goods
            .iter()
            .map(|&(g, amount)| GoodAmount {
                good: good(g),
                amount,
            })
            .collect(),
    }
}

fn names(capabilities: &[Capability]) -> Vec<String> {
    capabilities.iter().map(|c| c.name().to_owned()).collect()
}

/// What a rig or module does, as a phrase for the fitting panel.
fn effect(rule: &fitting::FittingRule) -> String {
    let effect = &rule.effect;
    let mut parts = Vec::new();
    if let Some(capacity) = effect.capacity {
        parts.push(format!("hold {capacity}"));
    }
    if effect.extra_capacity > 0 {
        parts.push(format!("hold +{}", effect.extra_capacity));
    }
    for &(material, rate) in effect.rates {
        parts.push(format!(
            "{} {rate:.1}/min",
            goods::Good::from_material(material).name()
        ));
    }
    if effect.scan_time < 1.0 {
        parts.push("fast scan".to_owned());
    }
    if effect.speed > 1.0 {
        parts.push(format!("{:.0}% faster", (effect.speed - 1.0) * 100.0));
    }
    if effect.weapon > 1.0 {
        parts.push(format!("weapon ×{:.0}", effect.weapon));
    }
    if effect.shield > 0.0 {
        parts.push(format!("shield {:.0}", effect.shield));
    }
    if effect.heat_capacity > 1.0 {
        parts.push(format!(
            "{:.0}% more extraction before cut-out",
            (effect.heat_capacity - 1.0) * 100.0
        ));
    }
    if effect.point_defence {
        parts.push("intercepts projectiles near the ship".to_owned());
    }
    if effect.deep_scan {
        parts.push("scans from the approach leg".to_owned());
    }
    parts.join(", ")
}

/// The outpost's rules and posted prices, for the client's previews.
#[must_use]
pub fn catalog() -> Catalog {
    Catalog {
        power_cap: STAGE1.power_cap,
        processor_batches: STAGE1.processor_batches,
        sockets: STAGE1
            .sockets
            .iter()
            .map(|socket| SocketRuleInfo {
                id: socket.id.to_owned(),
                name: socket.name.to_owned(),
                beside: socket.beside.to_owned(),
            })
            .collect(),
        facilities: STAGE1
            .facilities
            .iter()
            .map(|facility| FacilityInfo {
                id: facility.id.to_owned(),
                name: facility.name.to_owned(),
                tiers: facility
                    .tiers
                    .iter()
                    .map(|tier| TierInfo {
                        cost: cost(&tier.cost),
                        draw: tier.draw,
                        opens: names(tier.grants),
                        processor_batches: tier.processor_batches,
                    })
                    .collect(),
                rig: facility.rig.map(str::to_owned),
            })
            .collect(),
        adjacency: STAGE1
            .adjacency
            .iter()
            .map(|rule| AdjacencyInfo {
                facility: rule.facility.to_owned(),
                socket: rule.socket.to_owned(),
                bonus: match rule.bonus {
                    Bonus::ProcessorBatches(extra) => {
                        format!("processes {extra} more batches per run")
                    }
                    Bonus::FittingDiscount(percent) => {
                        format!("cuts rig and module prices by {percent}%")
                    }
                },
            })
            .collect(),
        reinstall: cost(&STAGE1.reinstall),
        fittings: FITTINGS
            .iter()
            .map(|rule| FittingInfo {
                id: rule.id.to_owned(),
                name: rule.name.to_owned(),
                slot: match rule.slot {
                    fitting::Slot::Rig => Slot::Rig,
                    fitting::Slot::Module => Slot::Module,
                },
                needs: rule.needs.name().to_owned(),
                price: rule.price,
                effect: effect(rule),
            })
            .collect(),
        prices: PRICES
            .iter()
            .map(|price| PriceInfo {
                good: good(price.good),
                buys_at: price.buys_at,
                sells_at: price.sells_at,
            })
            .collect(),
        recipes: RECIPES
            .iter()
            .map(|recipe| RecipeInfo {
                id: recipe.id.to_owned(),
                input: GoodAmount {
                    good: good(recipe.input.0),
                    amount: recipe.input.1,
                },
                output: GoodAmount {
                    good: good(recipe.output.0),
                    amount: recipe.output.1,
                },
            })
            .collect(),
        supplies: LAUNCH_COST,
    }
}

#[must_use]
pub fn account(account: &Account) -> AccountInfo {
    let outpost = &account.outpost;
    let discount = home::discount(account);
    let launch = home::launch(account);
    AccountInfo {
        credits: account.credits,
        ore: account.stock.ore,
        salvage: account.stock.salvage,
        volatiles: account.stock.volatiles,
        alloy: account.stock.alloy,
        components: account.stock.components,
        outpost: OutpostInfo {
            sockets: STAGE1
                .sockets
                .iter()
                .map(|socket| {
                    let installed = outpost.installed(socket.id);
                    SocketState {
                        id: socket.id.to_owned(),
                        open: STAGE1.is_open(outpost, socket),
                        facility: installed.map(|i| i.facility.clone()),
                        run: installed.map_or(0, |i| i.run),
                    }
                })
                .collect(),
            unlocks: outpost
                .unlocks
                .iter()
                .map(|unlock| UnlockInfo {
                    facility: unlock.facility.clone(),
                    tier: unlock.tier,
                })
                .collect(),
            power_used: STAGE1.power_used(outpost),
            opens: names(&home::capabilities(account)),
            processor_batches: STAGE1.processor_batches(outpost),
            fitting_discount: discount,
            open_tiers: home::open_tiers(account),
        },
        processing: account
            .processing
            .batches
            .iter()
            .map(|queued| QueuedInfo {
                recipe: queued.recipe.clone(),
                batches: queued.batches,
            })
            .collect(),
        hangar: account
            .hangar
            .pieces
            .iter()
            .map(|piece| piece.fitting.clone())
            .collect(),
        rig: account.hangar.saved.rig.clone(),
        modules: account.hangar.saved.modules.clone(),
        launch: LaunchInfo {
            flown: launch
                .flown
                .iter()
                .map(|piece| piece.fitting.clone())
                .collect(),
            missing: launch
                .missing
                .iter()
                .map(|(rule, why)| {
                    let price = fitting::discounted(rule.price, discount);
                    match why {
                        Missing::NotOwned => MissingInfo {
                            fitting: rule.id.to_owned(),
                            reason: format!("none in the hangar; {price} cr to buy"),
                            price: Some(price),
                        },
                        Missing::NeedsCapability(needs) => MissingInfo {
                            fitting: rule.id.to_owned(),
                            reason: format!("needs the {} powered", needs.name()),
                            price: None,
                        },
                    }
                })
                .collect(),
            stake: launch.stake(discount),
            supplies: LAUNCH_COST,
            capacity: launch.loadout().capacity,
        },
    }
}

#[must_use]
pub fn material(material: site::Material) -> Material {
    match material {
        site::Material::Ore => Material::Ore,
        site::Material::Salvage => Material::Salvage,
        site::Material::Volatiles => Material::Volatiles,
    }
}

#[must_use]
pub fn summary(summary: &run::Summary) -> RunSummary {
    let [ore, salvage, volatiles] = summary.banked;
    RunSummary {
        ending: match summary.ending {
            outcome::Ending::Docked => Ending::Docked,
            outcome::Ending::Destroyed => Ending::Destroyed,
            outcome::Ending::Abandoned => Ending::Abandoned,
        },
        ore,
        salvage,
        volatiles,
        payout: summary.payout,
        launch_cost: summary.launch_cost,
        cargo_lost: summary.lost.cargo,
        modules_lost: summary.lost.upgraded_modules,
        approach_seconds: seconds(summary.phases.approach),
        objective_seconds: seconds(summary.phases.objective),
        return_seconds: seconds(summary.phases.return_leg),
    }
}

/// The contract for the site `template` with `seed`, offered to a pilot who
/// has opened tiers up to `open_tiers` and whose layout grants `access`. It
/// shows only what preparation shows (design section 10): the hazard and the
/// dominant threat, never the seed.
#[must_use]
pub fn contract(
    template: SiteTemplate,
    seed: u32,
    open_tiers: u32,
    access: &[Capability],
) -> ServerMessage {
    // Hazard, threat and pockets are the same at every tier.
    let site = Site::generate(template, seed, ThreatTier::I, 1, access);
    ServerMessage::Contract {
        site: site_info(&site),
        hazard: match site.values.hazard {
            site::Hazard::ThermalFlare => Hazard::ThermalFlare,
            site::Hazard::DebrisDrift => Hazard::DebrisDrift,
            site::Hazard::IonFog => Hazard::IonFog,
        },
        dominant_threat: match site.values.dominant {
            site::DominantThreat::Drones => DominantThreat::Drones,
            site::DominantThreat::Raiders => DominantThreat::Raiders,
            site::DominantThreat::Balanced => DominantThreat::Balanced,
        },
        tiers: ThreatTier::ALL
            .iter()
            .map(|tier| TierOffer {
                tier: tier.number(),
                payout: tier.rules().payout,
                open: tier.number() <= open_tiers,
            })
            .collect(),
    }
}

#[must_use]
pub fn site_info(site: &Site) -> SiteInfo {
    let template = &site.template;
    SiteInfo {
        name: template.name.to_owned(),
        dock_x: template.dock.0,
        dock_z: template.dock.1,
        centre_x: template.centre.0,
        centre_z: template.centre.1,
        radius: template.radius,
        pockets: site
            .pockets
            .iter()
            .map(|pocket| PocketInfo {
                kind: match pocket.template.kind {
                    site::PocketKind::OreField => PocketKind::OreField,
                    site::PocketKind::Derelict => PocketKind::Derelict,
                    site::PocketKind::VolatileVeins => PocketKind::VolatileVeins,
                    site::PocketKind::NestCore => PocketKind::NestCore,
                },
                x: pocket.template.centre.0,
                z: pocket.template.centre.1,
                radius: pocket.template.radius,
                locked: pocket.locked,
                material: pocket.template.material.map(material),
            })
            .collect(),
        dock_radius: DOCK_RADIUS,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_contract_shows_preparation_values_and_opened_tiers_only() {
        let ServerMessage::Contract { site, tiers, .. } = contract(site::COMPACT_SITE, 3, 1, &[])
        else {
            panic!("expected a contract");
        };
        assert_eq!(site.pockets.len(), 4);
        assert_eq!(site.pockets.iter().filter(|p| p.locked).count(), 3);
        let open: Vec<u32> = tiers.iter().filter(|t| t.open).map(|t| t.tier).collect();
        assert_eq!(open, [1]);
        let json = serde_json::to_string(&contract(site::COMPACT_SITE, 3, 1, &[])).unwrap();
        assert!(!json.contains("seed") && !json.contains("rich"), "{json}");
    }

    #[test]
    fn a_layout_unlocks_its_pocket_in_the_contract() {
        let ServerMessage::Contract { site, .. } =
            contract(site::COMPACT_SITE, 3, 1, &[Capability::LocateSignal])
        else {
            panic!("expected a contract");
        };
        let derelict = site
            .pockets
            .iter()
            .find(|p| p.kind == PocketKind::Derelict)
            .unwrap();
        assert!(!derelict.locked);
        assert_eq!(site.pockets.iter().filter(|p| p.locked).count(), 2);
    }

    #[test]
    fn the_catalog_carries_every_rule_the_previews_need() {
        let catalog = catalog();
        assert_eq!(catalog.sockets.len(), 2);
        assert_eq!(catalog.facilities.len(), 3);
        let refinery = &catalog.facilities[1];
        assert_eq!(refinery.tiers[0].cost.credits, 150);
        assert!(
            refinery.tiers[0]
                .opens
                .contains(&"Volatile veins pocket".to_owned())
        );
        assert_eq!(refinery.tiers[1].processor_batches, Some(14));
        let extraction = catalog
            .fittings
            .iter()
            .find(|f| f.id == "extraction_rig")
            .unwrap();
        assert_eq!(extraction.effect, "hold 32, ore 3.0/min, volatiles 1.4/min");
        assert_eq!(catalog.prices.len(), 5);
    }

    /// Writes the catalog beside the generated types, so the client's preview
    /// tests run against the real rules. CI fails if the committed file is stale.
    #[test]
    fn exports_the_catalog_for_the_clients_tests() {
        let Ok(dir) = std::env::var("TS_RS_EXPORT_DIR") else {
            return;
        };
        let json = serde_json::to_string_pretty(&catalog()).unwrap();
        let contents = format!(
            "// This file was generated by frontier-server's tests. Do not edit this file manually.\n\n\
             import type {{ Catalog }} from \"./Catalog\";\n\n\
             /** The catalog the server sends after signing in. */\n\
             export const CATALOG: Catalog = {json};\n"
        );
        let path = std::path::Path::new(&dir).join("catalogFixture.ts");
        if std::fs::read_to_string(&path).ok().as_deref() != Some(contents.as_str()) {
            std::fs::write(path, contents).unwrap();
        }
    }

    #[test]
    fn a_home_shows_its_stake_and_what_stays_behind() {
        let mut home = Account::new("w", "Vega");
        home.hangar.add(fitting::fitting("scout_rig").unwrap());
        home.hangar
            .save(Some("scout_rig"), &["cargo_frame"])
            .unwrap();
        let info = account(&home);
        assert!(
            info.launch.flown.is_empty(),
            "no sensor array: basic fitting"
        );
        assert_eq!(info.launch.capacity, 20);
        assert_eq!(info.launch.missing.len(), 2);
        assert_eq!(info.launch.missing[0].reason, "needs the Scout rig powered");
        assert!(info.outpost.sockets[0].open);
        assert!(!info.outpost.sockets[1].open);
    }
}
