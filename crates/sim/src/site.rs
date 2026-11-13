//! The site: a template plus a seed plus parameters (design sections 2, 4 and
//! 10). The same template, seed, tier and crew always give the same site.
//!
//! Every number here is a placeholder from design section 10, or, where the
//! design gives none, a value chosen to fit its timings; the site geometry is
//! of the second kind.

use crate::collision::Obstacle;
use crate::rng::Rng;
use crate::{TICKS_PER_SECOND, ticks_for_millis};

/// What an outpost's powered layout grants an expedition (design section 10).
/// Expedition options are keyed to these tags, never to facility identifiers.
#[derive(
    Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord, serde::Serialize, serde::Deserialize,
)]
pub enum Capability {
    /// Locates the derelict's signal: from a sensor array.
    LocateSignal,
    /// Works volatile veins: from a refinery.
    WorkVolatiles,
    /// Breaches the nest core: from a fabrication bay.
    BreachNest,
    /// Fits the scout rig.
    ScoutRig,
    /// Fits the extraction rig.
    ExtractionRig,
    /// Fits the escort rig.
    EscortRig,
    /// Fits the cargo frame module.
    CargoFrame,
    /// Fits the deep-scan array module.
    DeepScan,
    /// Fits the heat sink module.
    HeatSink,
    /// Fits the point-defence module.
    PointDefence,
}

impl Capability {
    pub const ALL: [Self; 10] = [
        Self::LocateSignal,
        Self::WorkVolatiles,
        Self::BreachNest,
        Self::ScoutRig,
        Self::ExtractionRig,
        Self::EscortRig,
        Self::CargoFrame,
        Self::DeepScan,
        Self::HeatSink,
        Self::PointDefence,
    ];

    /// What a preview names: the pocket, rig or module this opens (design
    /// section 2).
    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            Self::LocateSignal => "Derelict pocket",
            Self::WorkVolatiles => "Volatile veins pocket",
            Self::BreachNest => "Nest core pocket",
            Self::ScoutRig => "Scout rig",
            Self::ExtractionRig => "Extraction rig",
            Self::EscortRig => "Escort rig",
            Self::CargoFrame => "Cargo frame module",
            Self::DeepScan => "Deep-scan array module",
            Self::HeatSink => "Heat sink module",
            Self::PointDefence => "Point-defence module",
        }
    }
}

/// The kinds of pocket in the Stage 1 site.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PocketKind {
    OreField,
    Derelict,
    VolatileVeins,
    NestCore,
}

/// Raw materials a site yields.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Material {
    Ore,
    Salvage,
    Volatiles,
}

impl Material {
    pub const ALL: [Self; 3] = [Self::Ore, Self::Salvage, Self::Volatiles];

    #[must_use]
    pub const fn index(self) -> usize {
        self as usize
    }
}

/// The site's hazard, drawn from the seed and shown in preparation.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Hazard {
    /// Heat builds 30% faster.
    ThermalFlare,
    /// Hitting an asteroid between pockets damages the hull.
    DebrisDrift,
    /// Scan and lock ranges are 40% shorter.
    IonFog,
}

impl Hazard {
    const ALL: [Self; 3] = [Self::ThermalFlare, Self::DebrisDrift, Self::IonFog];

    /// How much faster extraction heat builds.
    #[must_use]
    pub const fn heat_factor(self) -> f32 {
        match self {
            Self::ThermalFlare => 1.3,
            _ => 1.0,
        }
    }

    /// The share of scan and lock range that remains.
    #[must_use]
    pub const fn range_factor(self) -> f32 {
        match self {
            Self::IonFog => 0.6,
            _ => 1.0,
        }
    }
}

/// Which NPCs dominate the threat, drawn from the seed and shown in preparation.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum DominantThreat {
    Drones,
    Raiders,
    Balanced,
}

impl DominantThreat {
    const ALL: [Self; 3] = [Self::Drones, Self::Raiders, Self::Balanced];

    /// The chance that any one NPC of a wave is a drone rather than a raider.
    #[must_use]
    pub const fn drone_share(self) -> f32 {
        match self {
            Self::Drones => 0.8,
            Self::Raiders => 0.25,
            Self::Balanced => 0.5,
        }
    }
}

/// The contract's threat tiers.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum ThreatTier {
    I,
    II,
    III,
}

/// What a threat tier sets (design section 10).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct TierRules {
    /// Credits for the whole expedition before crew scaling.
    pub payout: u32,
    pub yield_factor: f32,
    /// NPCs present at launch before crew scaling.
    pub opening_threat: u32,
    /// How often the alert rises with time.
    pub alert_interval_ticks: u32,
    /// Multiplies every NPC's top speed and acceleration.
    pub npc_speed: f32,
    /// Multiplies every NPC's weapon damage.
    pub npc_damage: f32,
}

impl ThreatTier {
    pub const ALL: [Self; 3] = [Self::I, Self::II, Self::III];

    /// 1, 2 or 3.
    #[must_use]
    pub const fn number(self) -> u32 {
        self as u32 + 1
    }

    #[must_use]
    pub fn from_number(number: u32) -> Option<Self> {
        Self::ALL.into_iter().find(|tier| tier.number() == number)
    }

    /// The tier's values. NPC speed and damage are the owner's addition after
    /// the first playtest, not design section 10's: at full strength drones
    /// and raiders outran the starter hull and three drones broke it in about
    /// seven seconds, too much for a first run. The first cut, 0.7 and 0.5,
    /// was still too much: shots home, so three raiders took about three
    /// quarters of a starter hull in the time a pilot who never misses a
    /// beat needs to destroy them.
    /// Tier I NPCs now fly at half speed, so the starter hull leaves them
    /// behind without its cruise drive, and hit a quarter as hard; tiers II
    /// and III keep full strength, so the step up stays real. Threat still
    /// never scales with experience.
    #[must_use]
    pub const fn rules(self) -> TierRules {
        const MINUTE: u32 = 60 * TICKS_PER_SECOND;
        match self {
            Self::I => TierRules {
                payout: 100,
                yield_factor: 1.0,
                opening_threat: 3,
                alert_interval_ticks: 4 * MINUTE,
                npc_speed: 0.5,
                npc_damage: 0.25,
            },
            Self::II => TierRules {
                payout: 160,
                yield_factor: 1.25,
                opening_threat: 5,
                alert_interval_ticks: 3 * MINUTE,
                npc_speed: 1.0,
                npc_damage: 1.0,
            },
            Self::III => TierRules {
                payout: 240,
                yield_factor: 1.5,
                opening_threat: 8,
                alert_interval_ticks: 2 * MINUTE,
                npc_speed: 1.0,
                npc_damage: 1.0,
            },
        }
    }
}

/// How a crew of 1 to 4 scales threat, yield and payout (design section 10).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct CrewScaling {
    pub threat: f32,
    pub yield_pools: f32,
    pub payout: f32,
}

/// The crew table of design section 10. A crew outside 1 to 4 is clamped.
#[must_use]
pub fn crew_scaling(pilots: u32) -> CrewScaling {
    match pilots {
        0 | 1 => CrewScaling {
            threat: 1.0,
            yield_pools: 1.0,
            payout: 1.0,
        },
        2 => CrewScaling {
            threat: 1.5,
            yield_pools: 1.8,
            payout: 1.6,
        },
        3 => CrewScaling {
            threat: 1.9,
            yield_pools: 2.5,
            payout: 2.2,
        },
        _ => CrewScaling {
            threat: 2.2,
            yield_pools: 3.1,
            payout: 2.8,
        },
    }
}

/// A pocket as authored.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PocketTemplate {
    pub kind: PocketKind,
    pub centre: (f32, f32),
    pub radius: f32,
    /// The capability a crew needs to work it, if any.
    pub needs: Option<Capability>,
    /// What it yields by extraction. The nest core pays a bounty instead
    /// (plan M5), so it has none.
    pub material: Option<Material>,
    /// Units per run at tier I for a solo pilot.
    pub base_yield: f32,
    /// Units per minute in the basic fitting.
    pub base_rate: f32,
    /// How much faster heat builds while extracting here.
    pub heat_factor: f32,
    /// Where its deposits sit, relative to its centre.
    pub deposits: &'static [(f32, f32)],
}

/// A site as authored: where everything sits before the seed is applied.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SiteTemplate {
    pub name: &'static str,
    /// The pilots' home berth: launch and docking.
    pub dock: (f32, f32),
    /// Leaving this circle after working a pocket starts the return leg.
    pub centre: (f32, f32),
    pub radius: f32,
    pub pockets: &'static [PocketTemplate],
    /// Asteroids scattered between the dock and the site, and through it.
    pub asteroids: usize,
    /// How far ahead of a returning ship interceptors appear, metres.
    pub intercept_distance: f32,
    /// How far from its centre a scan of the site reaches, before hazards.
    pub scan_range: f32,
}

const ORE_DEPOSITS: &[(f32, f32)] = &[
    (0.0, 0.0),
    (24.0, 14.0),
    (-20.0, 18.0),
    (12.0, -26.0),
    (-26.0, -14.0),
];
const SALVAGE_DEPOSITS: &[(f32, f32)] = &[(0.0, 0.0), (14.0, 10.0), (-12.0, -10.0)];
const VOLATILE_DEPOSITS: &[(f32, f32)] = &[(0.0, 0.0), (18.0, -12.0), (-16.0, 12.0)];

const fn pockets(
    ore: (f32, f32),
    derelict: (f32, f32),
    veins: (f32, f32),
    nest: (f32, f32),
    scale: f32,
) -> [PocketTemplate; 4] {
    [
        PocketTemplate {
            kind: PocketKind::OreField,
            centre: ore,
            radius: 45.0 * scale,
            needs: None,
            material: Some(Material::Ore),
            base_yield: 40.0,
            base_rate: 2.0,
            heat_factor: 1.0,
            deposits: ORE_DEPOSITS,
        },
        PocketTemplate {
            kind: PocketKind::Derelict,
            centre: derelict,
            radius: 30.0 * scale,
            needs: Some(Capability::LocateSignal),
            material: Some(Material::Salvage),
            base_yield: 18.0,
            base_rate: 1.2,
            heat_factor: 1.0,
            deposits: SALVAGE_DEPOSITS,
        },
        PocketTemplate {
            kind: PocketKind::VolatileVeins,
            centre: veins,
            radius: 32.0 * scale,
            needs: Some(Capability::WorkVolatiles),
            material: Some(Material::Volatiles),
            base_yield: 16.0,
            base_rate: 0.8,
            heat_factor: 2.0,
            deposits: VOLATILE_DEPOSITS,
        },
        PocketTemplate {
            kind: PocketKind::NestCore,
            centre: nest,
            radius: 32.0 * scale,
            needs: Some(Capability::BreachNest),
            material: None,
            base_yield: 0.0,
            base_rate: 0.0,
            heat_factor: 1.0,
            deposits: &[],
        },
    ]
}

/// The Stage 1 site at design section 10's timings: about three minutes from
/// the dock to the ore field in the basic fitting, and three back.
pub const FRONTIER_SITE: SiteTemplate = SiteTemplate {
    name: "Kessler Drift",
    dock: (-900.0, 0.0),
    centre: (620.0, 0.0),
    radius: 360.0,
    pockets: &pockets(
        (560.0, -60.0),
        (720.0, 170.0),
        (820.0, -210.0),
        (420.0, 220.0),
        1.0,
    ),
    asteroids: 220,
    intercept_distance: 220.0,
    scan_range: 480.0,
};

/// The same site with short legs, for tests and for trying the loop quickly.
pub const COMPACT_SITE: SiteTemplate = SiteTemplate {
    name: "Kessler Drift (compact)",
    dock: (-120.0, 0.0),
    centre: (70.0, 0.0),
    radius: 125.0,
    pockets: &pockets(
        (45.0, -10.0),
        (115.0, 65.0),
        (125.0, -65.0),
        (20.0, 80.0),
        0.8,
    ),
    asteroids: 40,
    intercept_distance: 60.0,
    scan_range: 180.0,
};

/// The template with this name, for restoring a checkpointed run.
#[must_use]
pub fn template_named(name: &str) -> Option<SiteTemplate> {
    [FRONTIER_SITE, COMPACT_SITE]
        .into_iter()
        .find(|template| template.name == name)
}

/// One alert step's wave, before crew scaling (design section 10).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Wave {
    pub drones: u32,
    pub raiders: u32,
}

/// What the seed decides. Hazard and dominant threat are shown in
/// preparation; the rest is revealed by scanning in the site.
#[derive(Debug, Clone, PartialEq)]
pub struct SeedValues {
    pub hazard: Hazard,
    pub dominant: DominantThreat,
    /// Index of the pocket with 1.5 times its yield and rate.
    pub rich_pocket: usize,
    /// The wave for each alert step, in order.
    pub waves: Vec<Wave>,
    /// When the alert rises with time, in ticks after the objective starts.
    pub rises: Vec<u32>,
    /// The two pockets holding the relays.
    pub relays: [usize; 2],
}

/// The richest a pocket can be, relative to its template.
pub const RICH_FACTOR: f32 = 1.5;
/// Alert steps planned ahead: enough for the 45-minute limit at tier III.
const PLANNED_RISES: usize = 24;

/// A deposit: a fixed point in a pocket holding a finite amount of one material.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Deposit {
    pub pocket: usize,
    pub material: Material,
    pub x: f32,
    pub z: f32,
    pub remaining: f32,
}

/// A pocket as generated for this run.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Pocket {
    pub template: PocketTemplate,
    /// Whether the crew lacks the capability it needs.
    pub locked: bool,
    pub rich: bool,
    /// Units per minute in the basic fitting.
    pub rate: f32,
    /// Units the run's deposits hold between them.
    pub pool: f32,
}

impl Pocket {
    #[must_use]
    pub fn contains(&self, x: f32, z: f32) -> bool {
        let (cx, cz) = self.template.centre;
        (x - cx).hypot(z - cz) <= self.template.radius
    }
}

/// One run's site.
#[derive(Debug, Clone, PartialEq)]
pub struct Site {
    pub template: SiteTemplate,
    pub seed: u32,
    pub tier: ThreatTier,
    pub pilots: u32,
    /// What the crew's outposts grant between them.
    pub access: Vec<Capability>,
    pub values: SeedValues,
    pub pockets: Vec<Pocket>,
    pub deposits: Vec<Deposit>,
    pub obstacles: Vec<Obstacle>,
}

/// Keeps asteroids this far from deposits, pocket edges excepted.
const DEPOSIT_CLEARANCE: f32 = 8.0;
/// Keeps asteroids this far from the dock.
const DOCK_CLEARANCE: f32 = 30.0;

impl Site {
    /// Generates the site for a crew of `pilots` at `tier`, whose outposts grant
    /// `access` between them (design section 2: access is shared).
    #[must_use]
    pub fn generate(
        template: SiteTemplate,
        seed: u32,
        tier: ThreatTier,
        pilots: u32,
        access: &[Capability],
    ) -> Self {
        let values = seed_values(&template, seed, tier);
        let rules = tier.rules();
        let crew = crew_scaling(pilots);
        let pockets: Vec<Pocket> = template
            .pockets
            .iter()
            .enumerate()
            .map(|(index, &pocket)| {
                let rich = index == values.rich_pocket;
                let factor = if rich { RICH_FACTOR } else { 1.0 };
                Pocket {
                    template: pocket,
                    locked: pocket.needs.is_some_and(|needs| !access.contains(&needs)),
                    rich,
                    rate: pocket.base_rate * factor,
                    pool: pocket.base_yield * factor * rules.yield_factor * crew.yield_pools,
                }
            })
            .collect();
        let deposits = pockets
            .iter()
            .enumerate()
            .filter(|(_, pocket)| !pocket.locked)
            .flat_map(|(index, pocket)| {
                let offsets = pocket.template.deposits;
                let share = pocket.pool / offsets.len().max(1) as f32;
                let (cx, cz) = pocket.template.centre;
                pocket
                    .template
                    .material
                    .into_iter()
                    .flat_map(move |material| {
                        offsets.iter().map(move |&(dx, dz)| Deposit {
                            pocket: index,
                            material,
                            x: cx + dx * pocket.template.radius / 45.0,
                            z: cz + dz * pocket.template.radius / 45.0,
                            remaining: share,
                        })
                    })
            })
            .collect::<Vec<_>>();
        let obstacles = asteroids(&template, seed, &deposits);
        Self {
            template,
            seed,
            tier,
            pilots,
            access: access.to_vec(),
            values,
            pockets,
            deposits,
            obstacles,
        }
    }

    /// The pocket containing a point, if any.
    #[must_use]
    pub fn pocket_at(&self, x: f32, z: f32) -> Option<usize> {
        self.pockets.iter().position(|pocket| pocket.contains(x, z))
    }

    /// Whether a point is inside the site's circle.
    #[must_use]
    pub fn contains(&self, x: f32, z: f32) -> bool {
        let (cx, cz) = self.template.centre;
        (x - cx).hypot(z - cz) <= self.template.radius
    }

    /// The contract payout for the whole expedition: tier times crew scaling.
    #[must_use]
    pub fn payout(&self) -> u32 {
        let payout = self.tier.rules().payout as f32 * crew_scaling(self.pilots).payout;
        // A few hundred credits at most, so `as` loses nothing.
        payout.round() as u32
    }

    /// NPCs in the opening threat, after crew scaling.
    #[must_use]
    pub fn opening_threat(&self) -> u32 {
        scaled(self.tier.rules().opening_threat, self.pilots)
    }

    /// The NPCs one alert step adds: one plus the tier number, after crew scaling.
    #[must_use]
    pub fn wave_size(&self) -> u32 {
        scaled(1 + self.tier.number(), self.pilots)
    }

    /// The wave for alert step `level` (1 for the first step), at full size.
    #[must_use]
    pub fn wave(&self, level: u32) -> Wave {
        let planned = &self.values.waves;
        let index = (level.max(1) as usize - 1).min(planned.len().saturating_sub(1));
        planned.get(index).copied().unwrap_or(Wave {
            drones: self.wave_size(),
            raiders: 0,
        })
    }
}

fn scaled(count: u32, pilots: u32) -> u32 {
    // Small counts, so `as` loses nothing.
    (count as f32 * crew_scaling(pilots).threat).round() as u32
}

fn pick<T: Copy>(rng: &mut Rng, items: &[T]) -> T {
    // `unit` is below 1, so the index is within the slice.
    let index = (rng.unit() * items.len() as f32) as usize;
    items[index.min(items.len() - 1)]
}

fn seed_values(template: &SiteTemplate, seed: u32, tier: ThreatTier) -> SeedValues {
    let mut rng = Rng::new(seed);
    let hazard = pick(&mut rng, &Hazard::ALL);
    let dominant = pick(&mut rng, &DominantThreat::ALL);
    let indices: Vec<usize> = (0..template.pockets.len()).collect();
    let rich_pocket = pick(&mut rng, &indices);
    let first_relay = pick(&mut rng, &indices);
    let others: Vec<usize> = indices
        .iter()
        .copied()
        .filter(|&i| i != first_relay)
        .collect();
    let relays = [first_relay, pick(&mut rng, &others)];

    let interval = tier.rules().alert_interval_ticks;
    // Each rise lands within a quarter interval of its schedule.
    let jitter = interval / 4;
    let rises = (1..=PLANNED_RISES as u32)
        .map(|step| {
            let offset = (rng.unit() * (2 * jitter) as f32) as u32;
            step * interval - jitter + offset
        })
        .collect();
    let size = 1 + tier.number();
    let waves = (0..PLANNED_RISES)
        .map(|_| {
            let drones = (0..size)
                .filter(|_| rng.unit() < dominant.drone_share())
                .count() as u32;
            Wave {
                drones,
                raiders: size - drones,
            }
        })
        .collect();
    SeedValues {
        hazard,
        dominant,
        rich_pocket,
        waves,
        rises,
        relays,
    }
}

fn asteroids(template: &SiteTemplate, seed: u32, deposits: &[Deposit]) -> Vec<Obstacle> {
    // A different stream from the seed values, so adding rocks never changes them.
    let mut rng = Rng::new(seed ^ 0x9e37_79b9);
    let (dx, dz) = template.dock;
    let (cx, cz) = template.centre;
    let min_x = dx.min(cx - template.radius);
    let max_x = dx.max(cx + template.radius);
    let half_depth = template.radius;
    let mut field = Vec::with_capacity(template.asteroids);
    let mut attempts = 0;
    while field.len() < template.asteroids && attempts < template.asteroids * 20 {
        attempts += 1;
        let x = rng.between(min_x, max_x);
        let z = rng.between(cz - half_depth, cz + half_depth);
        let radius = rng.between(0.8, 5.0);
        let clear_of_dock = (x - dx).hypot(z - dz) > DOCK_CLEARANCE + radius;
        let clear_of_deposits = deposits
            .iter()
            .all(|d| (x - d.x).hypot(z - d.z) > DEPOSIT_CLEARANCE + radius);
        let clear_of_pocket_centres = template.pockets.iter().all(|pocket| {
            (x - pocket.centre.0).hypot(z - pocket.centre.1) > DEPOSIT_CLEARANCE + radius
        });
        if clear_of_dock && clear_of_deposits && clear_of_pocket_centres {
            field.push(Obstacle { x, z, radius });
        }
    }
    field
}

/// Ticks in `minutes`, for readable constants.
#[must_use]
pub const fn minutes(minutes: u32) -> u32 {
    ticks_for_millis(minutes * 60_000)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ship::ARENA_HALF_WIDTH;

    fn tier_i(seed: u32) -> Site {
        Site::generate(FRONTIER_SITE, seed, ThreatTier::I, 1, &[])
    }

    #[test]
    fn the_same_seed_gives_the_same_site_and_another_differs() {
        assert_eq!(tier_i(7), tier_i(7));
        let differs = (8..20).any(|seed| tier_i(seed).values != tier_i(7).values);
        assert!(differs);
        assert_ne!(tier_i(7).obstacles, tier_i(8).obstacles);
    }

    #[test]
    fn every_seed_value_occurs_across_seeds() {
        let sites: Vec<Site> = (0..200).map(tier_i).collect();
        for hazard in Hazard::ALL {
            assert!(
                sites.iter().any(|s| s.values.hazard == hazard),
                "{hazard:?}"
            );
        }
        for dominant in DominantThreat::ALL {
            assert!(
                sites.iter().any(|s| s.values.dominant == dominant),
                "{dominant:?}"
            );
        }
        for pocket in 0..4 {
            assert!(sites.iter().any(|s| s.values.rich_pocket == pocket));
        }
        assert!(
            sites
                .iter()
                .all(|s| s.values.relays[0] != s.values.relays[1])
        );
    }

    #[test]
    fn a_basic_crew_can_enter_only_the_ore_field() {
        let site = tier_i(1);
        let open: Vec<PocketKind> = site
            .pockets
            .iter()
            .filter(|pocket| !pocket.locked)
            .map(|pocket| pocket.template.kind)
            .collect();
        assert_eq!(open, [PocketKind::OreField]);
        assert!(site.deposits.iter().all(|d| d.material == Material::Ore));
        let all = [
            Capability::LocateSignal,
            Capability::WorkVolatiles,
            Capability::BreachNest,
        ];
        let opened = Site::generate(FRONTIER_SITE, 1, ThreatTier::I, 1, &all);
        assert!(opened.pockets.iter().all(|pocket| !pocket.locked));
    }

    #[test]
    fn yields_follow_the_tier_crew_and_rich_pocket() {
        let site = tier_i(1);
        let ore = &site.pockets[0];
        let expected = if ore.rich { 60.0 } else { 40.0 };
        assert!((ore.pool - expected).abs() < 1e-3);
        let total: f32 = site.deposits.iter().map(|d| d.remaining).sum();
        assert!((total - expected).abs() < 1e-3);

        let crew = Site::generate(FRONTIER_SITE, 1, ThreatTier::III, 4, &[]);
        assert!((crew.pockets[0].pool / ore.pool - 1.5 * 3.1).abs() < 1e-3);
        assert_eq!(crew.payout(), 672, "240 cr times 2.8");
        assert_eq!(crew.opening_threat(), 18, "8 times 2.2, rounded");
        assert_eq!(site.payout(), 100);
        assert_eq!(site.opening_threat(), 3);
        assert_eq!(site.wave_size(), 2);
    }

    #[test]
    fn waves_follow_the_tier_and_rises_follow_its_interval() {
        let site = Site::generate(FRONTIER_SITE, 3, ThreatTier::II, 1, &[]);
        for wave in &site.values.waves {
            assert_eq!(wave.drones + wave.raiders, 3);
        }
        let interval = ThreatTier::II.rules().alert_interval_ticks;
        for (step, rise) in site.values.rises.iter().enumerate() {
            let scheduled = (step as u32 + 1) * interval;
            assert!(rise.abs_diff(scheduled) <= interval / 4);
        }
        assert!(site.values.rises.windows(2).all(|pair| pair[0] < pair[1]));
    }

    #[test]
    fn the_site_fits_the_arena_and_keeps_its_dock_and_deposits_clear() {
        for template in [FRONTIER_SITE, COMPACT_SITE] {
            let site = Site::generate(template, 5, ThreatTier::I, 1, &[]);
            assert!(!site.contains(template.dock.0, template.dock.1));
            for pocket in &site.pockets {
                let (x, z) = pocket.template.centre;
                assert!(site.contains(x, z), "{:?}", pocket.template.kind);
                assert!(x.abs() + pocket.template.radius < ARENA_HALF_WIDTH);
            }
            for rock in &site.obstacles {
                for deposit in &site.deposits {
                    assert!((rock.x - deposit.x).hypot(rock.z - deposit.z) > rock.radius + 6.0);
                }
                let (dx, dz) = template.dock;
                assert!((rock.x - dx).hypot(rock.z - dz) > rock.radius + 20.0);
            }
        }
    }

    #[test]
    fn the_frontier_approach_is_about_three_minutes_at_full_speed() {
        let (dx, dz) = FRONTIER_SITE.dock;
        let (ox, oz) = FRONTIER_SITE.pockets[0].centre;
        let seconds = (ox - dx).hypot(oz - dz) / crate::ship::STARTER_HANDLING.max_speed;
        assert!((150.0..=180.0).contains(&seconds), "{seconds} s");
    }

    #[test]
    fn crew_scaling_matches_the_table() {
        assert_eq!(crew_scaling(2).threat, 1.5);
        assert_eq!(crew_scaling(3).yield_pools, 2.5);
        assert_eq!(crew_scaling(4).payout, 2.8);
        assert_eq!(crew_scaling(9), crew_scaling(4));
    }
}
