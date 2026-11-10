//! Translations from the simulation's types to the wire's. `frontier-sim`
//! knows nothing of the protocol, so the server maps one to the other.

use frontier_protocol::{
    DominantThreat, Ending, Hazard, Material, PocketInfo, PocketKind, RunSummary, ServerMessage,
    SiteInfo, TierOffer,
};
use frontier_sim::outcome;
use frontier_sim::run::{self, DOCK_RADIUS, seconds};
use frontier_sim::site::{self, Site, SiteTemplate, ThreatTier};

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
/// has opened tiers up to `open_tiers`. It shows only what preparation shows
/// (design section 10): the hazard and the dominant threat, never the seed.
#[must_use]
pub fn contract(template: SiteTemplate, seed: u32, open_tiers: u32) -> ServerMessage {
    // Hazard, threat and pockets are the same at every tier.
    let site = Site::generate(template, seed, ThreatTier::I, 1, &[]);
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

fn site_info(site: &Site) -> SiteInfo {
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
        let ServerMessage::Contract { site, tiers, .. } = contract(site::COMPACT_SITE, 3, 1) else {
            panic!("expected a contract");
        };
        assert_eq!(site.pockets.len(), 4);
        assert_eq!(site.pockets.iter().filter(|p| p.locked).count(), 3);
        let open: Vec<u32> = tiers.iter().filter(|t| t.open).map(|t| t.tier).collect();
        assert_eq!(open, [1]);
        let json = serde_json::to_string(&contract(site::COMPACT_SITE, 3, 1)).unwrap();
        assert!(!json.contains("seed") && !json.contains("rich"), "{json}");
    }
}
