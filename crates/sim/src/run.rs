//! One run of an expedition (design sections 2, 6 and 10): pilots launch from
//! the dock, fly the approach leg, work the site's pockets under rising threat,
//! fly the return leg, and dock to bank their cargo. The instance ends when
//! every pilot has docked, been destroyed or been abandoned, or at its limit.
//!
//! The run keeps an event log of everything that matters to the economy and to
//! the Stage 1 gates; the server writes it out.

use crate::TICKS_PER_SECOND;
use crate::Tick;
use crate::extraction::{
    BASIC_CAPACITY, Conditions, EXTRACT_RANGE, Extractor, ExtractorState, MAX_EXTRACT_SPEED,
    Refusal, Stop,
};
use crate::outcome::{Ending, Loadout, losses};
use crate::profile::ShipKind;
use crate::rng::Rng;
use crate::ship::ShipState;
use crate::ship::{ARENA_HALF_WIDTH, ShipInput};
use crate::site::{Hazard, Material, Site, ThreatTier, minutes, template_named};
use crate::skirmish::MAX_NPCS;
use crate::world::{ShipId, World};
use serde::{Deserialize, Serialize};

/// The instance ends this long after launch (design section 10).
pub const INSTANCE_LIMIT_TICKS: u32 = minutes(45);
/// Pilots are warned this long after launch (design section 10).
pub const LIMIT_WARNING_TICKS: u32 = minutes(40);
/// How far a pilot can lock a target before hazards, metres. A placeholder.
pub const LOCK_RANGE: f32 = 60.0;
/// A site scan in the basic fitting. A placeholder.
pub const SCAN_TICKS: u32 = 15 * TICKS_PER_SECOND;
/// The fastest a ship may move and keep scanning, metres per second.
pub const MAX_SCAN_SPEED: f32 = 1.0;
/// Leaving this circle around the dock counts as undocking, metres.
pub const UNDOCK_RADIUS: f32 = 30.0;
/// Entering this circle around the dock after undocking docks the ship.
pub const DOCK_RADIUS: f32 = 15.0;
/// The contract pays when a ship docks with this much site cargo (design 10).
pub const CONTRACT_MIN_CARGO: u32 = 10;
/// What each pilot's launch supplies cost, credits (design section 10).
pub const LAUNCH_COST: u32 = 40;
/// Hull lost per m/s of impact with debris between pockets. A placeholder.
pub const DEBRIS_DAMAGE_PER_MPS: f32 = 3.0;
/// A disconnected ship stays in play this long out of combat (design 10).
pub const EXPOSURE_TICKS: u32 = 10 * TICKS_PER_SECOND;
/// A disconnected ship stays in play this long in combat (design 10).
pub const COMBAT_EXPOSURE_TICKS: u32 = 30 * TICKS_PER_SECOND;
/// A ship counts as in combat this long after a shot, a hit or a hostile lock.
pub const COMBAT_MEMORY_TICKS: u32 = 10 * TICKS_PER_SECOND;
/// How long a disconnected pilot may return before the run is abandoned
/// (design section 10's reconnect grace window).
pub const GRACE_TICKS: u32 = minutes(5);
/// A run is checkpointed this often, and whenever a pilot enters a pocket
/// (design section 10).
pub const CHECKPOINT_TICKS: u32 = minutes(1);
/// Impacts slower than this are scrapes and do no damage, m/s.
const MIN_DEBRIS_IMPACT: f32 = 1.0;

/// Where a pilot is in the run.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Phase {
    /// Launched and still at the dock.
    AtDock,
    /// Undocked; not yet in a pocket.
    Approach,
    /// In the site, having entered a pocket.
    Objective,
    /// Left the site after working it, heading home.
    Return,
    Docked,
    Destroyed,
    Abandoned,
}

impl Phase {
    #[must_use]
    pub const fn is_over(self) -> bool {
        matches!(self, Self::Docked | Self::Destroyed | Self::Abandoned)
    }
}

/// Why the alert rose.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AlertCause {
    Time,
    CutOut,
}

/// Why the instance ended.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EndReason {
    /// Every pilot docked, was destroyed or was abandoned.
    AllDone,
    /// The instance limit was reached.
    Limit,
}

/// Ticks a pilot spent in each flown phase (design section 2's travel time:
/// undock to first pocket, last pocket to dock).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct PhaseTimes {
    pub approach: u32,
    pub objective: u32,
    pub return_leg: u32,
}

/// Units of each material, indexed by [`Material::index`].
pub type Cargo = [u32; 3];

/// How a pilot's run ended, and what it brought.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Summary {
    pub ending: Ending,
    /// Cargo banked at home; empty unless docked.
    pub banked: Cargo,
    /// This pilot's share of the contract pot, credits.
    pub payout: u32,
    pub launch_cost: u32,
    pub lost: Loadout,
    pub phases: PhaseTimes,
}

/// Something that happened in the run, for the event log.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum RunEvent {
    Launched {
        pilots: u32,
    },
    /// Launch supplies used up: credits leave the economy.
    SuppliesSpent {
        pilot: ShipId,
        credits: u32,
    },
    Undocked {
        pilot: ShipId,
    },
    EnteredPocket {
        pilot: ShipId,
        pocket: usize,
    },
    LeftPocket {
        pilot: ShipId,
        pocket: usize,
    },
    ReturnLeg {
        pilot: ShipId,
    },
    /// Material created in a ship's hold.
    Extracted {
        pilot: ShipId,
        material: Material,
        units: u32,
    },
    ExtractorStopped {
        pilot: ShipId,
        why: Stop,
    },
    Vented {
        pilot: ShipId,
    },
    ScanStarted {
        pilot: ShipId,
    },
    ScanInterrupted {
        pilot: ShipId,
    },
    Scanned {
        pilot: ShipId,
    },
    AlertRose {
        level: u32,
        cause: AlertCause,
    },
    WaveArrived {
        level: u32,
        drones: u32,
        raiders: u32,
    },
    Intercepted {
        pilot: ShipId,
        raiders: u32,
    },
    HazardDamage {
        pilot: ShipId,
        amount: f32,
    },
    /// Cargo moved into the home's stock, and credits paid.
    Docked {
        pilot: ShipId,
        summary: Summary,
    },
    /// Cargo destroyed with the ship.
    Destroyed {
        pilot: ShipId,
        summary: Summary,
    },
    /// Cargo lost with an abandoned run.
    Abandoned {
        pilot: ShipId,
        summary: Summary,
    },
    LimitWarning,
    /// The pilot's connection dropped; their ship stays in play this many seconds.
    Disconnected {
        pilot: ShipId,
        exposed_for: u32,
    },
    /// The exposure timer ran out: the ship is held inert and cannot be targeted.
    HeldInert {
        pilot: ShipId,
    },
    Reconnected {
        pilot: ShipId,
    },
    /// The run was restored from a checkpoint after a server restart.
    Restored,
    Ended {
        reason: EndReason,
    },
}

/// What a pilot's client shows about their own ship and the run.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PilotStatus {
    pub phase: Phase,
    /// Whether the cruise drive may engage, and if not, why.
    pub drive: Drive,
    pub cargo: u32,
    pub capacity: u32,
    pub heat: f32,
    pub extractor: ExtractorState,
    /// Progress of a scan in hand, 0 to 1.
    pub scan: Option<f32>,
    pub alert: u32,
    /// Ticks since launch.
    pub elapsed: u32,
}

/// Whether a pilot's cruise drive may engage this tick, and if not, why.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Drive {
    #[default]
    Ready,
    /// Inside the site's circle: pockets are flown at normal speed.
    InSite,
    /// A hostile has the ship locked: so return-leg raiders still catch it.
    Hunted,
    /// The extractor is working.
    Extracting,
}

/// Settings a run is launched with besides its site.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Options {
    /// Replaces the site's opening threat, up to the NPC cap; for benchmarks.
    pub opening_threat: Option<u32>,
    /// What each pilot actually paid for supplies, when it was less than
    /// [`LAUNCH_COST`]: a pilot short of credits pays what they have.
    pub supplies_paid: Option<u32>,
}

#[derive(Debug, Clone)]
struct PilotRun {
    ship: ShipId,
    phase: Phase,
    cargo: Cargo,
    capacity: u32,
    extractor: Extractor,
    scan: Option<u32>,
    pocket: Option<usize>,
    intercepted: bool,
    undocked_at: Option<u32>,
    first_pocket_at: Option<u32>,
    last_pocket_exit_at: Option<u32>,
    payout: u32,
    /// The pilot holds the cruise control.
    cruise_requested: bool,
    drive: Drive,
    /// When the pilot's connection dropped, while they are away.
    disconnected_at: Option<u32>,
    /// When a disconnected pilot's ship is held inert.
    exposure_ends: u32,
    /// The ship as it was when it was held inert, out of the world.
    inert: Option<(ShipState, f32)>,
    /// The last tick the ship fired, was hit, or was locked by a hostile.
    last_combat: Option<u32>,
}

impl PilotRun {
    fn total(&self) -> u32 {
        self.cargo.iter().sum()
    }

    fn times(&self, end: u32) -> PhaseTimes {
        let undocked = self.undocked_at.unwrap_or(end);
        let Some(first) = self.first_pocket_at else {
            return PhaseTimes {
                approach: end - undocked,
                ..PhaseTimes::default()
            };
        };
        let objective_end = if self.pocket.is_some() {
            end
        } else {
            self.last_pocket_exit_at.unwrap_or(end)
        };
        PhaseTimes {
            approach: first.saturating_sub(undocked),
            objective: objective_end.saturating_sub(first),
            return_leg: end.saturating_sub(objective_end),
        }
    }
}

/// A pilot as a checkpoint keeps them.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PilotCheckpoint {
    pub ship: u16,
    pub phase: Phase,
    pub cargo: Cargo,
    pub capacity: u32,
    pub x: f32,
    pub z: f32,
    pub heading: f32,
    pub hull: f32,
    pub undocked_at: Option<u32>,
    pub first_pocket_at: Option<u32>,
    pub last_pocket_exit_at: Option<u32>,
    pub intercepted: bool,
}

/// A run as a checkpoint keeps it: enough to restore it after a server
/// restart (design section 6). NPCs and shots in flight are not kept.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Checkpoint {
    pub template: String,
    pub seed: u32,
    pub tier: u32,
    pub pilots: u32,
    pub elapsed: u32,
    pub alert: u32,
    pub rises_done: u32,
    pub objective_started: Option<u32>,
    pub revealed: bool,
    pub contract_paid: bool,
    pub warned: bool,
    pub launch_cost: u32,
    pub deposits: Vec<f32>,
    pub crew: Vec<PilotCheckpoint>,
}

/// One expedition instance from launch to its end.
#[derive(Debug, Clone)]
pub struct Run {
    world: World,
    site: Site,
    pilots: Vec<PilotRun>,
    alert: u32,
    /// The tick the first pilot entered a pocket: the alert clock starts here.
    objective_started: Option<u32>,
    rises_done: usize,
    revealed: bool,
    contract_paid: bool,
    warned: bool,
    ended: Option<EndReason>,
    events: Vec<(u32, RunEvent)>,
    /// What each pilot paid for supplies.
    launch_cost: u32,
    /// A checkpoint is due at the end of this tick.
    checkpoint_due: bool,
}

impl Run {
    /// Launches `pilots` ships from the site's dock and places the opening
    /// threat. Returns the run and the pilots' ships, in order.
    #[must_use]
    pub fn launch(site: Site, pilots: u32, options: Options) -> (Self, Vec<ShipId>) {
        let mut world = World::with_obstacles(site.obstacles.clone());
        let (dx, dz) = site.template.dock;
        let (cx, cz) = site.template.centre;
        let heading = (cz - dz).atan2(cx - dx);
        let ships: Vec<ShipId> = (0..pilots)
            .map(|i| {
                // Side by side across the launch direction, 4 m apart.
                let offset = (i as f32 - (pilots as f32 - 1.0) / 2.0) * 4.0;
                let home = (dx - heading.sin() * offset, dz + heading.cos() * offset);
                world.spawn_pilot_at(home, heading)
            })
            .collect();
        let mut run = Self {
            world,
            pilots: ships
                .iter()
                .map(|&ship| PilotRun {
                    ship,
                    phase: Phase::AtDock,
                    cargo: [0; 3],
                    capacity: BASIC_CAPACITY,
                    extractor: Extractor::default(),
                    scan: None,
                    pocket: None,
                    intercepted: false,
                    undocked_at: None,
                    first_pocket_at: None,
                    last_pocket_exit_at: None,
                    payout: 0,
                    cruise_requested: false,
                    drive: Drive::Ready,
                    disconnected_at: None,
                    exposure_ends: 0,
                    inert: None,
                    last_combat: None,
                })
                .collect(),
            site,
            alert: 0,
            objective_started: None,
            rises_done: 0,
            revealed: false,
            contract_paid: false,
            warned: false,
            ended: None,
            events: Vec::new(),
            launch_cost: options.supplies_paid.unwrap_or(LAUNCH_COST),
            checkpoint_due: false,
        };
        run.log(RunEvent::Launched { pilots });
        for &pilot in &ships {
            run.log(RunEvent::SuppliesSpent {
                pilot,
                credits: run.launch_cost,
            });
        }
        let opening = options
            .opening_threat
            .unwrap_or_else(|| run.site.opening_threat());
        run.place_opening_threat(opening);
        (run, ships)
    }

    #[must_use]
    pub fn world(&self) -> &World {
        &self.world
    }

    #[must_use]
    pub fn site(&self) -> &Site {
        &self.site
    }

    #[must_use]
    pub fn alert(&self) -> u32 {
        self.alert
    }

    /// Whether a scan has revealed the hidden seed values to the crew.
    #[must_use]
    pub fn revealed(&self) -> bool {
        self.revealed
    }

    /// The tick the alert clock started, if any pilot has reached a pocket.
    #[must_use]
    pub fn objective_started(&self) -> Option<u32> {
        self.objective_started
    }

    #[must_use]
    pub fn ended(&self) -> Option<EndReason> {
        self.ended
    }

    /// How far a pilot can lock a target here.
    #[must_use]
    pub fn lock_range(&self) -> f32 {
        LOCK_RANGE * self.site.values.hazard.range_factor()
    }

    /// How far from the site's centre a scan reaches here.
    #[must_use]
    pub fn scan_range(&self) -> f32 {
        self.site.template.scan_range * self.site.values.hazard.range_factor()
    }

    /// Ticks since launch.
    #[must_use]
    pub fn elapsed(&self) -> u32 {
        // An instance lasts at most 45 minutes: 54,000 ticks.
        self.world.tick().get() as u32
    }

    /// Takes the events logged since the last call, each with its tick.
    pub fn take_events(&mut self) -> Vec<(u32, RunEvent)> {
        std::mem::take(&mut self.events)
    }

    #[must_use]
    pub fn status(&self, ship: ShipId) -> Option<PilotStatus> {
        let pilot = self.pilot(ship)?;
        Some(PilotStatus {
            phase: pilot.phase,
            cargo: pilot.total(),
            capacity: pilot.capacity,
            heat: pilot.extractor.heat,
            extractor: pilot.extractor.state,
            scan: pilot.scan.map(|ticks| ticks as f32 / SCAN_TICKS as f32),
            alert: self.alert,
            elapsed: self.elapsed(),
            drive: pilot.drive,
        })
    }

    /// Sets a pilot's controls for the next step.
    /// Sets a pilot's controls for the next step. A cruise request takes
    /// effect only where the drive may engage ([`Drive`]).
    pub fn set_controls(&mut self, ship: ShipId, input: ShipInput, firing: bool) {
        if !self.flying(ship) {
            return;
        }
        if let Some(index) = self.index(ship) {
            self.pilots[index].cruise_requested = input.cruise;
            let allowed = self.pilots[index].drive == Drive::Ready;
            self.world.set_input(
                ship,
                ShipInput {
                    cruise: input.cruise && allowed,
                    ..input
                },
            );
        }
        self.world.set_firing(ship, firing);
    }

    /// Decides each pilot's drive for the coming step, and drops the cruise of
    /// any pilot who may no longer use it.
    fn gate_drives(&mut self) {
        for index in 0..self.pilots.len() {
            let pilot = &self.pilots[index];
            if pilot.phase.is_over() {
                continue;
            }
            let ship = pilot.ship;
            let Some(state) = self.world.ship(ship).map(|s| s.state) else {
                continue;
            };
            let hunted = self
                .world
                .ships()
                .iter()
                .any(|other| other.kind != ShipKind::Pilot && other.lock == Some(ship));
            let drive = if self.site.contains(state.x, state.z) {
                Drive::InSite
            } else if pilot.extractor.is_on() {
                Drive::Extracting
            } else if hunted {
                Drive::Hunted
            } else {
                Drive::Ready
            };
            let cruise = pilot.cruise_requested && drive == Drive::Ready;
            self.pilots[index].drive = drive;
            if let Some(input) = self.world.ship(ship).map(|s| s.input) {
                self.world.set_input(ship, ShipInput { cruise, ..input });
            }
        }
    }

    /// Locks a hostile within lock range, or releases the lock. Returns
    /// whether the requested lock was taken. Lock range limits only taking a
    /// lock: once held it lasts until its target is gone, so a target that
    /// strays out for a moment is not silently lost.
    pub fn set_lock(&mut self, ship: ShipId, target: Option<ShipId>) -> bool {
        if !self.flying(ship) {
            return false;
        }
        let in_range = target.is_none_or(|target| self.within_lock_range(ship, target));
        if in_range {
            self.world.set_lock(ship, target)
        } else {
            self.world.set_lock(ship, None);
            false
        }
    }

    /// Starts the extractor at the nearest deposit in reach, or stops it.
    ///
    /// # Errors
    /// Refuses as [`Extractor::start`] does; a pilot who is not flying is
    /// refused as not at a deposit.
    pub fn extract(&mut self, ship: ShipId, on: bool) -> Result<(), Refusal> {
        if !self.flying(ship) {
            return Err(Refusal::NotAtDeposit);
        }
        let Some(index) = self.index(ship) else {
            return Err(Refusal::NotAtDeposit);
        };
        if !on {
            self.pilots[index].extractor.stop();
            return Ok(());
        }
        let state = self
            .world
            .ship(ship)
            .map(|s| s.state)
            .ok_or(Refusal::NotAtDeposit)?;
        let deposit = self
            .site
            .deposits
            .iter()
            .enumerate()
            .map(|(i, d)| (i, (d.x - state.x).hypot(d.z - state.z)))
            .filter(|&(_, distance)| distance <= EXTRACT_RANGE)
            .min_by(|a, b| a.1.total_cmp(&b.1))
            .map(|(i, _)| i)
            .ok_or(Refusal::NotAtDeposit)?;
        let conditions = self.conditions(index, deposit);
        self.pilots[index].extractor.start(deposit, &conditions)?;
        if self.pilots[index].scan.take().is_some() {
            self.log(RunEvent::ScanInterrupted { pilot: ship });
        }
        Ok(())
    }

    /// Vents the extractor. Returns false if it is cutting out or venting.
    pub fn vent(&mut self, ship: ShipId) -> bool {
        let Some(index) = self.index(ship).filter(|_| self.flying(ship)) else {
            return false;
        };
        let vented = self.pilots[index].extractor.vent();
        if vented {
            self.log(RunEvent::Vented { pilot: ship });
        }
        vented
    }

    /// Starts a scan of the site. It needs the ship nearly still and within
    /// scan range of the site, and stops the extractor. Returns false if it
    /// cannot start or the site is already revealed.
    pub fn scan(&mut self, ship: ShipId) -> bool {
        let Some(index) = self.index(ship).filter(|_| self.flying(ship)) else {
            return false;
        };
        if self.revealed || self.pilots[index].scan.is_some() || !self.can_scan(ship) {
            return false;
        }
        self.pilots[index].extractor.stop();
        self.pilots[index].scan = Some(0);
        self.log(RunEvent::ScanStarted { pilot: ship });
        true
    }

    /// Takes a pilot out of the run, as when they disconnect. In Stage 1's
    /// first build the run is abandoned at once; reconnection is plan M4.
    pub fn leave(&mut self, ship: ShipId) {
        let Some(index) = self.index(ship) else {
            return;
        };
        if self.pilots[index].phase.is_over() {
            return;
        }
        self.world.remove_ship(ship);
        self.finish(index, Ending::Abandoned);
        self.check_end();
    }

    /// The pilot's connection dropped. Their ship stays in play, exposed, for
    /// 30 seconds if it has been in combat and 10 otherwise, then is held inert;
    /// the run is theirs to resume for the grace window (design section 10).
    pub fn disconnect(&mut self, ship: ShipId) {
        let Some(index) = self.index(ship) else {
            return;
        };
        let now = self.elapsed();
        let pilot = &mut self.pilots[index];
        if pilot.phase.is_over() || pilot.disconnected_at.is_some() {
            return;
        }
        let in_combat = pilot
            .last_combat
            .is_some_and(|tick| now.saturating_sub(tick) < COMBAT_MEMORY_TICKS);
        let exposure = if in_combat {
            COMBAT_EXPOSURE_TICKS
        } else {
            EXPOSURE_TICKS
        };
        pilot.disconnected_at = Some(now);
        pilot.exposure_ends = now + exposure;
        pilot.cruise_requested = false;
        pilot.extractor.stop();
        pilot.scan = None;
        self.world.set_input(ship, ShipInput::default());
        self.world.set_firing(ship, false);
        self.log(RunEvent::Disconnected {
            pilot: ship,
            exposed_for: exposure / TICKS_PER_SECOND,
        });
    }

    /// The pilot is back: an inert ship returns where it was held. Returns
    /// whether the pilot is still in the run.
    pub fn reconnect(&mut self, ship: ShipId) -> bool {
        let Some(index) = self.index(ship) else {
            return false;
        };
        if self.pilots[index].phase.is_over() || self.ended.is_some() {
            return false;
        }
        if self.pilots[index].disconnected_at.take().is_some() {
            if let Some((state, hull)) = self.pilots[index].inert.take() {
                self.world.restore_pilot(ship, state, hull);
            }
            self.log(RunEvent::Reconnected { pilot: ship });
        }
        true
    }

    /// Whether the pilot is away, waiting to reconnect.
    #[must_use]
    pub fn is_disconnected(&self, ship: ShipId) -> bool {
        self.pilot(ship)
            .is_some_and(|pilot| pilot.disconnected_at.is_some() && !pilot.phase.is_over())
    }

    /// Takes a checkpoint if one is due: on entering a pocket and every minute.
    pub fn take_checkpoint(&mut self) -> Option<Checkpoint> {
        if !std::mem::take(&mut self.checkpoint_due) || self.ended.is_some() {
            return None;
        }
        Some(self.checkpoint())
    }

    /// The run as it stands, enough to restore it after a restart.
    #[must_use]
    pub fn checkpoint(&self) -> Checkpoint {
        Checkpoint {
            template: self.site.template.name.to_owned(),
            seed: self.site.seed,
            tier: self.site.tier.number(),
            pilots: self.site.pilots,
            elapsed: self.elapsed(),
            alert: self.alert,
            rises_done: u32::try_from(self.rises_done).unwrap_or(u32::MAX),
            objective_started: self.objective_started,
            revealed: self.revealed,
            contract_paid: self.contract_paid,
            warned: self.warned,
            launch_cost: self.launch_cost,
            deposits: self.site.deposits.iter().map(|d| d.remaining).collect(),
            crew: self
                .pilots
                .iter()
                .map(|pilot| {
                    let (state, hull) = self
                        .world
                        .ship(pilot.ship)
                        .map(|ship| (ship.state, ship.hull))
                        .or(pilot.inert)
                        .unwrap_or_default();
                    PilotCheckpoint {
                        ship: pilot.ship.0,
                        phase: pilot.phase,
                        cargo: pilot.cargo,
                        capacity: pilot.capacity,
                        x: state.x,
                        z: state.z,
                        heading: state.heading,
                        hull,
                        undocked_at: pilot.undocked_at,
                        first_pocket_at: pilot.first_pocket_at,
                        last_pocket_exit_at: pilot.last_pocket_exit_at,
                        intercepted: pilot.intercepted,
                    }
                })
                .collect(),
        }
    }

    /// Rebuilds a run from its last checkpoint after a server restart (design
    /// section 6). Each pilot's ship waits, held inert where it was, for its
    /// pilot to reconnect within the grace window. The NPCs are not restored
    /// (the owner's decision): the alert and its clock carry on, so the next
    /// wave arrives on time. Returns `None` for a checkpoint of an unknown site.
    #[must_use]
    pub fn restore(checkpoint: &Checkpoint) -> Option<Self> {
        let template = template_named(&checkpoint.template)?;
        let tier = ThreatTier::from_number(checkpoint.tier)?;
        let mut site = Site::generate(template, checkpoint.seed, tier, checkpoint.pilots, &[]);
        for (deposit, remaining) in site.deposits.iter_mut().zip(&checkpoint.deposits) {
            deposit.remaining = *remaining;
        }
        let mut world = World::with_obstacles(site.obstacles.clone());
        world.set_tick(Tick::new(u64::from(checkpoint.elapsed)));
        let now = checkpoint.elapsed;
        let pilots = checkpoint
            .crew
            .iter()
            .map(|saved| {
                let away = !saved.phase.is_over();
                let state = ShipState {
                    x: saved.x,
                    z: saved.z,
                    heading: saved.heading,
                    speed: 0.0,
                };
                PilotRun {
                    ship: ShipId(saved.ship),
                    phase: saved.phase,
                    cargo: saved.cargo,
                    capacity: saved.capacity,
                    extractor: Extractor::default(),
                    scan: None,
                    pocket: None,
                    intercepted: saved.intercepted,
                    undocked_at: saved.undocked_at,
                    first_pocket_at: saved.first_pocket_at,
                    last_pocket_exit_at: saved.last_pocket_exit_at,
                    payout: 0,
                    cruise_requested: false,
                    drive: Drive::Ready,
                    disconnected_at: away.then_some(now),
                    exposure_ends: now,
                    inert: away.then_some((state, saved.hull)),
                    last_combat: None,
                }
            })
            .collect();
        let mut run = Self {
            world,
            site,
            pilots,
            alert: checkpoint.alert,
            objective_started: checkpoint.objective_started,
            rises_done: checkpoint.rises_done as usize,
            revealed: checkpoint.revealed,
            contract_paid: checkpoint.contract_paid,
            warned: checkpoint.warned,
            ended: None,
            events: Vec::new(),
            launch_cost: checkpoint.launch_cost,
            checkpoint_due: false,
        };
        run.log(RunEvent::Restored);
        Some(run)
    }

    /// Notes which pilots fired or were shot at this step, for the exposure timer.
    fn note_combat(&mut self) {
        let now = self.elapsed();
        let fired: Vec<(ShipId, ShipId)> = self
            .world
            .events()
            .iter()
            .filter_map(|event| match *event {
                crate::combat::Event::Fired {
                    shooter, target, ..
                } => Some((shooter, target)),
                crate::combat::Event::Destroyed { .. } => None,
            })
            .collect();
        for pilot in &mut self.pilots {
            if fired
                .iter()
                .any(|&(shooter, target)| shooter == pilot.ship || target == pilot.ship)
                || pilot.drive == Drive::Hunted
            {
                pilot.last_combat = Some(now);
            }
        }
    }

    /// Holds a disconnected pilot's ship inert when its exposure ends, and
    /// abandons their run when the grace window closes.
    fn hold_disconnected(&mut self) {
        let now = self.elapsed();
        for index in 0..self.pilots.len() {
            let pilot = &self.pilots[index];
            let Some(since) = pilot.disconnected_at else {
                continue;
            };
            if pilot.phase.is_over() {
                continue;
            }
            let ship = pilot.ship;
            if now >= since + GRACE_TICKS {
                self.world.remove_ship(ship);
                self.pilots[index].inert = None;
                self.finish(index, Ending::Abandoned);
            } else if pilot.inert.is_none() && now >= pilot.exposure_ends {
                let held = self.world.ship(ship).map(|s| (s.state, s.hull));
                if let Some(held) = held {
                    self.pilots[index].inert = Some(held);
                    self.world.remove_ship(ship);
                    self.log(RunEvent::HeldInert { pilot: ship });
                }
            }
        }
    }

    /// Advances the run by one tick.
    pub fn step(&mut self) {
        if self.ended.is_some() {
            return;
        }
        self.rise_with_time();
        self.gate_drives();
        self.world.step();
        self.note_combat();
        self.debris_damage();
        self.record_destroyed();
        self.hold_disconnected();
        for index in 0..self.pilots.len() {
            if self.pilots[index].phase.is_over() {
                continue;
            }
            self.move_pilot(index);
            self.work_extractor(index);
            self.work_scan(index);
        }
        let elapsed = self.elapsed();
        if elapsed.is_multiple_of(CHECKPOINT_TICKS) {
            self.checkpoint_due = true;
        }
        if !self.warned && elapsed >= LIMIT_WARNING_TICKS {
            self.warned = true;
            self.log(RunEvent::LimitWarning);
        }
        if elapsed >= INSTANCE_LIMIT_TICKS {
            for index in 0..self.pilots.len() {
                if !self.pilots[index].phase.is_over() {
                    let ship = self.pilots[index].ship;
                    self.world.remove_ship(ship);
                    self.finish(index, Ending::Abandoned);
                }
            }
            self.end(EndReason::Limit);
        }
        self.check_end();
    }

    fn log(&mut self, event: RunEvent) {
        let tick = self.elapsed();
        self.events.push((tick, event));
    }

    fn index(&self, ship: ShipId) -> Option<usize> {
        self.pilots.iter().position(|pilot| pilot.ship == ship)
    }

    fn pilot(&self, ship: ShipId) -> Option<&PilotRun> {
        self.pilots.iter().find(|pilot| pilot.ship == ship)
    }

    fn flying(&self, ship: ShipId) -> bool {
        self.ended.is_none() && self.pilot(ship).is_some_and(|pilot| !pilot.phase.is_over())
    }

    fn within_lock_range(&self, ship: ShipId, target: ShipId) -> bool {
        let (Some(a), Some(b)) = (self.world.ship(ship), self.world.ship(target)) else {
            return false;
        };
        (a.state.x - b.state.x).hypot(a.state.z - b.state.z) <= self.lock_range()
    }

    fn can_scan(&self, ship: ShipId) -> bool {
        let Some(state) = self.world.ship(ship).map(|s| s.state) else {
            return false;
        };
        let (cx, cz) = self.site.template.centre;
        state.speed.abs() <= MAX_SCAN_SPEED
            && (state.x - cx).hypot(state.z - cz) <= self.scan_range()
    }

    fn conditions(&self, index: usize, deposit: usize) -> Conditions {
        let pilot = &self.pilots[index];
        let state = self.world.ship(pilot.ship).map(|s| s.state);
        let Some(d) = self.site.deposits.get(deposit) else {
            return Conditions {
                at_deposit: false,
                remaining: 0.0,
                space: 0,
                rate: 0.0,
                heat_factor: 1.0,
            };
        };
        let pocket = &self.site.pockets[d.pocket];
        let at_deposit = state.is_some_and(|s| {
            (d.x - s.x).hypot(d.z - s.z) <= EXTRACT_RANGE && s.speed.abs() <= MAX_EXTRACT_SPEED
        });
        Conditions {
            at_deposit,
            remaining: d.remaining,
            space: pilot.capacity.saturating_sub(pilot.total()),
            rate: pocket.rate,
            heat_factor: self.site.values.hazard.heat_factor() * pocket.template.heat_factor,
        }
    }

    fn rise_with_time(&mut self) {
        let Some(started) = self.objective_started else {
            return;
        };
        let since = self.elapsed().saturating_sub(started);
        while let Some(&rise) = self.site.values.rises.get(self.rises_done) {
            if since < rise {
                break;
            }
            self.rises_done += 1;
            self.raise_alert(AlertCause::Time);
        }
    }

    /// Raises the alert one step and brings in its wave (design section 10).
    fn raise_alert(&mut self, cause: AlertCause) {
        self.alert += 1;
        let level = self.alert;
        self.log(RunEvent::AlertRose { level, cause });
        let planned = self.site.wave(level);
        let threat = crate::site::crew_scaling(self.site.pilots).threat;
        // Small counts, so `as` loses nothing.
        let scale = |count: u32| (count as f32 * threat).round() as u32;
        let (drones, raiders) = (scale(planned.drones), scale(planned.raiders));

        let target = self.busiest_pocket();
        let home = self.site.pockets[target].template.centre;
        let mut rng = Rng::new(self.site.seed ^ level.wrapping_mul(0x9e37_79b9));
        let bearing = rng.between(0.0, std::f32::consts::TAU);
        let (cx, cz) = self.site.template.centre;
        let edge = self.site.template.radius + 40.0;
        let entry = clamp_to_arena((cx + bearing.cos() * edge, cz + bearing.sin() * edge));
        let mut spawned = (0, 0);
        for (kind, count) in [(ShipKind::Drone, drones), (ShipKind::Raider, raiders)] {
            for i in 0..count {
                if self.npc_count() >= MAX_NPCS {
                    break;
                }
                let spread = i as f32 * 3.0;
                let at = clamp_to_arena((entry.0 + spread, entry.1 - spread));
                self.world.spawn_npc_at(kind, at, home);
                if kind == ShipKind::Drone {
                    spawned.0 += 1;
                } else {
                    spawned.1 += 1;
                }
            }
        }
        self.log(RunEvent::WaveArrived {
            level,
            drones: spawned.0,
            raiders: spawned.1,
        });
    }

    /// The pocket with the most pilots near it; the first pocket if none.
    fn busiest_pocket(&self) -> usize {
        let mut counts = vec![0_u32; self.site.pockets.len()];
        for pilot in self.pilots.iter().filter(|p| !p.phase.is_over()) {
            let Some(state) = self.world.ship(pilot.ship).map(|s| s.state) else {
                continue;
            };
            let nearest = self
                .site
                .pockets
                .iter()
                .enumerate()
                .min_by(|(_, a), (_, b)| {
                    let d = |p: &crate::site::Pocket| {
                        (p.template.centre.0 - state.x).hypot(p.template.centre.1 - state.z)
                    };
                    d(a).total_cmp(&d(b))
                })
                .map(|(i, _)| i);
            if let Some(i) = nearest {
                counts[i] += 1;
            }
        }
        counts
            .iter()
            .enumerate()
            .max_by_key(|&(i, count)| (*count, std::cmp::Reverse(i)))
            .map_or(0, |(i, _)| i)
    }

    fn npc_count(&self) -> usize {
        self.world
            .ships()
            .iter()
            .filter(|ship| ship.kind != ShipKind::Pilot)
            .count()
    }

    fn place_opening_threat(&mut self, count: u32) {
        let mut rng = Rng::new(self.site.seed ^ 0x5bd1_e995);
        let share = self.site.values.dominant.drone_share();
        let ore = self.site.pockets[0].template;
        let (cx, cz) = self.site.template.centre;
        for i in 0..count.min(MAX_NPCS as u32) {
            let angle = rng.between(0.0, std::f32::consts::TAU);
            if rng.unit() < share {
                // Drones gather in the ore field (design section 10).
                let r = ore.radius * 0.6;
                let home = (
                    ore.centre.0 + angle.cos() * r,
                    ore.centre.1 + angle.sin() * r,
                );
                self.world.spawn_npc(ShipKind::Drone, home);
            } else {
                let r = 30.0 + i as f32 * 5.0;
                let home = (cx + angle.cos() * r, cz + angle.sin() * r);
                self.world.spawn_npc(ShipKind::Raider, home);
            }
        }
    }

    fn debris_damage(&mut self) {
        if self.site.values.hazard != Hazard::DebrisDrift {
            return;
        }
        let hits: Vec<(ShipId, f32)> = self
            .world
            .impacts()
            .iter()
            .copied()
            .filter(|&(ship, impact)| {
                impact >= MIN_DEBRIS_IMPACT
                    && self.index(ship).is_some()
                    && self
                        .world
                        .ship(ship)
                        .is_some_and(|s| self.site.pocket_at(s.state.x, s.state.z).is_none())
            })
            .collect();
        for (pilot, impact) in hits {
            let amount = impact * DEBRIS_DAMAGE_PER_MPS;
            self.log(RunEvent::HazardDamage { pilot, amount });
            self.world.damage(pilot, amount);
        }
    }

    /// Ends the run of every pilot whose ship is gone: destroyed in combat or
    /// by a hazard.
    fn record_destroyed(&mut self) {
        for index in 0..self.pilots.len() {
            let pilot = &self.pilots[index];
            // An inert ship is out of the world on purpose.
            if !pilot.phase.is_over()
                && pilot.inert.is_none()
                && self.world.ship(pilot.ship).is_none()
            {
                self.finish(index, Ending::Destroyed);
            }
        }
    }

    fn move_pilot(&mut self, index: usize) {
        let ship = self.pilots[index].ship;
        let Some(state) = self.world.ship(ship).map(|s| s.state) else {
            return;
        };
        let now = self.elapsed();
        let (dx, dz) = self.site.template.dock;
        let from_dock = (state.x - dx).hypot(state.z - dz);

        let pocket = self.site.pocket_at(state.x, state.z);
        let was = self.pilots[index].pocket;
        if pocket != was {
            if let Some(left) = was {
                self.pilots[index].last_pocket_exit_at = Some(now);
                self.log(RunEvent::LeftPocket {
                    pilot: ship,
                    pocket: left,
                });
            }
            if let Some(entered) = pocket {
                self.log(RunEvent::EnteredPocket {
                    pilot: ship,
                    pocket: entered,
                });
                self.checkpoint_due = true;
            }
            self.pilots[index].pocket = pocket;
        }

        let in_site = self.site.contains(state.x, state.z);
        let phase = self.pilots[index].phase;
        match phase {
            Phase::AtDock if from_dock > UNDOCK_RADIUS => {
                self.pilots[index].phase = Phase::Approach;
                self.pilots[index].undocked_at = Some(now);
                self.log(RunEvent::Undocked { pilot: ship });
            }
            Phase::Approach if pocket.is_some() => {
                self.pilots[index].phase = Phase::Objective;
                self.pilots[index].first_pocket_at = Some(now);
                self.objective_started.get_or_insert(now);
            }
            Phase::Objective if !in_site => {
                self.pilots[index].phase = Phase::Return;
                self.log(RunEvent::ReturnLeg { pilot: ship });
                self.intercept(index);
            }
            Phase::Return if in_site => self.pilots[index].phase = Phase::Objective,
            _ => {}
        }
        let undocked = !matches!(self.pilots[index].phase, Phase::AtDock);
        if undocked && from_dock <= DOCK_RADIUS {
            self.world.remove_ship(ship);
            self.finish(index, Ending::Docked);
        }
    }

    /// On the return leg one raider intercepts for each full third of cargo
    /// capacity carried (design section 10), once per pilot.
    fn intercept(&mut self, index: usize) {
        if self.pilots[index].intercepted {
            return;
        }
        self.pilots[index].intercepted = true;
        let pilot = &self.pilots[index];
        let raiders = pilot.total() * 3 / pilot.capacity.max(1);
        let ship = pilot.ship;
        let Some(state) = self.world.ship(ship).map(|s| s.state) else {
            return;
        };
        if raiders == 0 {
            return;
        }
        let (dx, dz) = self.site.template.dock;
        let (vx, vz) = (dx - state.x, dz - state.z);
        let length = vx.hypot(vz).max(1.0);
        let ahead = self.site.template.intercept_distance.min(length * 0.8);
        let point = clamp_to_arena((state.x + vx / length * ahead, state.z + vz / length * ahead));
        let mut spawned = 0;
        for i in 0..raiders {
            if self.npc_count() >= MAX_NPCS {
                break;
            }
            let side = (i as f32 - (raiders as f32 - 1.0) / 2.0) * 6.0;
            let at = clamp_to_arena((point.0 - vz / length * side, point.1 + vx / length * side));
            self.world.spawn_npc_at(ShipKind::Raider, at, at);
            spawned += 1;
        }
        self.log(RunEvent::Intercepted {
            pilot: ship,
            raiders: spawned,
        });
    }

    fn work_extractor(&mut self, index: usize) {
        let ship = self.pilots[index].ship;
        let conditions = match self.pilots[index].extractor.deposit() {
            Some(deposit) => self.conditions(index, deposit),
            None => self.conditions(index, usize::MAX),
        };
        let deposit = self.pilots[index].extractor.deposit();
        let out = self.pilots[index].extractor.step(&conditions);
        if let Some(d) = deposit.and_then(|d| self.site.deposits.get_mut(d)) {
            d.remaining = (d.remaining - out.taken).max(0.0);
            if out.units > 0 {
                let material = d.material;
                self.pilots[index].cargo[material.index()] += out.units;
                self.log(RunEvent::Extracted {
                    pilot: ship,
                    material,
                    units: out.units,
                });
            }
        }
        if let Some(why) = out.stopped {
            self.log(RunEvent::ExtractorStopped { pilot: ship, why });
            if why == Stop::CutOut {
                // Every cut-out raises the alert (design section 10).
                self.raise_alert(AlertCause::CutOut);
            }
        }
        let total = self.pilots[index].total();
        let extracting = self.pilots[index].extractor.is_on();
        self.world.set_extracting(ship, extracting);
        // Raiders hunt the ship carrying the most cargo.
        self.world.set_cargo(ship, total);
    }

    fn work_scan(&mut self, index: usize) {
        let ship = self.pilots[index].ship;
        let Some(progress) = self.pilots[index].scan else {
            return;
        };
        if !self.can_scan(ship) {
            self.pilots[index].scan = None;
            self.log(RunEvent::ScanInterrupted { pilot: ship });
            return;
        }
        if progress + 1 >= SCAN_TICKS {
            self.pilots[index].scan = None;
            self.revealed = true;
            self.log(RunEvent::Scanned { pilot: ship });
        } else {
            self.pilots[index].scan = Some(progress + 1);
        }
    }

    /// Ends a pilot's run with `ending`, logging what it brought or cost.
    fn finish(&mut self, index: usize, ending: Ending) {
        let now = self.elapsed();
        let pilot = &self.pilots[index];
        let ship = pilot.ship;
        let carried = pilot.total();
        let phases = pilot.times(now);
        let mut payout = 0;
        if ending == Ending::Docked && carried >= CONTRACT_MIN_CARGO && !self.contract_paid {
            self.contract_paid = true;
            payout = self.site.payout() / self.site.pilots.max(1);
        }
        let banked = if ending == Ending::Docked {
            pilot.cargo
        } else {
            [0; 3]
        };
        let summary = Summary {
            ending,
            banked,
            payout,
            launch_cost: self.launch_cost,
            lost: losses(
                ending,
                Loadout {
                    upgraded_modules: 0,
                    cargo: carried,
                },
            ),
            phases,
        };
        let pilot = &mut self.pilots[index];
        pilot.phase = match ending {
            Ending::Docked => Phase::Docked,
            Ending::Destroyed => Phase::Destroyed,
            Ending::Abandoned => Phase::Abandoned,
        };
        pilot.payout = payout;
        pilot.extractor = Extractor::default();
        pilot.scan = None;
        pilot.cargo = [0; 3];
        self.log(match ending {
            Ending::Docked => RunEvent::Docked {
                pilot: ship,
                summary,
            },
            Ending::Destroyed => RunEvent::Destroyed {
                pilot: ship,
                summary,
            },
            Ending::Abandoned => RunEvent::Abandoned {
                pilot: ship,
                summary,
            },
        });
    }

    fn check_end(&mut self) {
        if self.ended.is_none() && self.pilots.iter().all(|pilot| pilot.phase.is_over()) {
            self.end(EndReason::AllDone);
        }
    }

    fn end(&mut self, reason: EndReason) {
        if self.ended.is_none() {
            self.ended = Some(reason);
            self.log(RunEvent::Ended { reason });
        }
    }
}

fn clamp_to_arena((x, z): (f32, f32)) -> (f32, f32) {
    let limit = ARENA_HALF_WIDTH - 10.0;
    (x.clamp(-limit, limit), z.clamp(-limit, limit))
}

/// Ticks as whole seconds, for summaries.
#[must_use]
pub fn seconds(ticks: u32) -> u32 {
    ticks / TICKS_PER_SECOND
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::site::{COMPACT_SITE, Capability, ThreatTier};

    fn quiet(seed: u32) -> (Run, ShipId) {
        let site = Site::generate(COMPACT_SITE, seed, ThreatTier::I, 1, &[]);
        let (run, ships) = Run::launch(
            site,
            1,
            Options {
                opening_threat: Some(0),
                ..Options::default()
            },
        );
        (run, ships[0])
    }

    fn place(run: &mut Run, ship: ShipId, (x, z): (f32, f32)) {
        let state = &mut run.world.ship_mut(ship).expect("the ship is flying").state;
        state.x = x;
        state.z = z;
        state.speed = 0.0;
    }

    fn steps(run: &mut Run, ticks: u32) {
        for _ in 0..ticks {
            run.step();
        }
    }

    fn events(run: &mut Run) -> Vec<RunEvent> {
        run.take_events()
            .into_iter()
            .map(|(_, event)| event)
            .collect()
    }

    /// Takes the pilot from the dock into the ore field, at its first deposit.
    fn at_deposit(run: &mut Run, ship: ShipId) {
        place(run, ship, (-60.0, 0.0));
        run.step();
        let deposit = run.site.deposits[0];
        place(run, ship, (deposit.x, deposit.z));
        run.step();
    }

    #[test]
    fn launches_pilots_at_the_dock_with_the_opening_threat() {
        let site = Site::generate(COMPACT_SITE, 4, ThreatTier::I, 1, &[]);
        let (mut run, ships) = Run::launch(site, 1, Options::default());
        let npcs = run
            .world
            .ships()
            .iter()
            .filter(|s| s.kind != ShipKind::Pilot);
        assert_eq!(npcs.count(), 3);
        let state = run.world.ship(ships[0]).unwrap().state;
        let (dx, dz) = COMPACT_SITE.dock;
        assert!((state.x - dx).hypot(state.z - dz) < 1.0);
        assert_eq!(run.status(ships[0]).unwrap().phase, Phase::AtDock);
        assert_eq!(
            events(&mut run),
            [
                RunEvent::Launched { pilots: 1 },
                RunEvent::SuppliesSpent {
                    pilot: ships[0],
                    credits: LAUNCH_COST
                }
            ]
        );
    }

    #[test]
    fn a_pilot_short_of_credits_launches_on_what_they_paid() {
        let site = Site::generate(COMPACT_SITE, 4, ThreatTier::I, 1, &[]);
        let options = Options {
            opening_threat: Some(0),
            supplies_paid: Some(25),
        };
        let (mut run, ships) = Run::launch(site, 1, options);
        assert!(events(&mut run).contains(&RunEvent::SuppliesSpent {
            pilot: ships[0],
            credits: 25
        }));
        run.leave(ships[0]);
        let summary = events(&mut run).into_iter().find_map(|event| match event {
            RunEvent::Abandoned { summary, .. } => Some(summary),
            _ => None,
        });
        assert_eq!(summary.map(|s| s.launch_cost), Some(25));
    }

    #[test]
    fn a_run_goes_out_works_a_pocket_comes_back_and_docks() {
        let (mut run, ship) = quiet(1);
        steps(&mut run, 20);
        place(&mut run, ship, (-60.0, 0.0));
        run.step();
        assert_eq!(run.status(ship).unwrap().phase, Phase::Approach);
        steps(&mut run, 100);
        at_deposit(&mut run, ship);
        assert_eq!(run.status(ship).unwrap().phase, Phase::Objective);
        assert!(run.objective_started().is_some());
        steps(&mut run, 200);
        place(&mut run, ship, (-80.0, 0.0));
        run.step();
        assert_eq!(run.status(ship).unwrap().phase, Phase::Return);
        steps(&mut run, 60);
        place(&mut run, ship, COMPACT_SITE.dock);
        run.step();
        assert_eq!(run.ended(), Some(EndReason::AllDone));
        let docked = events(&mut run).into_iter().find_map(|event| match event {
            RunEvent::Docked { summary, .. } => Some(summary),
            _ => None,
        });
        let summary = docked.expect("the pilot docked");
        assert_eq!(summary.ending, Ending::Docked);
        assert_eq!(summary.payout, 0, "no cargo, no contract");
        assert_eq!(summary.phases.approach, 102, "100 steps and two placements");
        assert_eq!(summary.phases.objective, 201);
        assert_eq!(summary.phases.return_leg, 61);
    }

    #[test]
    fn extraction_fills_the_hold_and_an_unattended_extractor_stops() {
        let (mut run, ship) = quiet(2);
        at_deposit(&mut run, ship);
        assert_eq!(run.extract(ship, true), Ok(()));
        let mut cut_out = false;
        while !cut_out {
            run.step();
            cut_out = matches!(
                run.status(ship).unwrap().extractor,
                ExtractorState::CutOut { .. }
            );
        }
        let cargo = run.status(ship).unwrap().cargo;
        assert!(cargo >= 1, "it yielded");
        // The cut-out's wave is another test's business.
        let npcs: Vec<ShipId> = run
            .world
            .ships()
            .iter()
            .filter(|s| s.kind != ShipKind::Pilot)
            .map(|s| s.id)
            .collect();
        for npc in npcs {
            run.world.remove_ship(npc);
        }
        steps(&mut run, 2 * crate::extraction::CUT_OUT_TICKS);
        let status = run.status(ship).unwrap();
        assert_eq!(
            status.extractor,
            ExtractorState::Off,
            "cut out and stayed off"
        );
        assert_eq!(status.cargo, cargo, "nothing without input");
        let log = events(&mut run);
        assert!(
            log.iter()
                .any(|e| matches!(e, RunEvent::Extracted { units: 1, .. }))
        );
        assert!(log.iter().any(|e| matches!(
            e,
            RunEvent::AlertRose {
                cause: AlertCause::CutOut,
                ..
            }
        )));
    }

    #[test]
    fn a_cut_out_raises_the_alert_and_brings_a_wave() {
        let (mut run, ship) = quiet(3);
        at_deposit(&mut run, ship);
        run.extract(ship, true).unwrap();
        let npcs = |run: &Run| {
            run.world
                .ships()
                .iter()
                .filter(|s| s.kind != ShipKind::Pilot)
                .count()
        };
        assert_eq!(npcs(&run), 0);
        let heat = crate::extraction::HEAT_TICKS as f32 / run.site.values.hazard.heat_factor();
        steps(&mut run, heat.ceil() as u32 + 1);
        assert_eq!(run.alert(), 1);
        assert_eq!(npcs(&run), run.site.wave_size() as usize);
    }

    #[test]
    fn the_alert_rises_with_time_once_the_objective_starts() {
        let (mut run, ship) = quiet(5);
        steps(&mut run, ThreatTier::I.rules().alert_interval_ticks * 2);
        assert_eq!(run.alert(), 0, "the clock waits for the first pocket");
        at_deposit(&mut run, ship);
        let first = run.site.values.rises[0];
        steps(&mut run, first + 1);
        assert_eq!(run.alert(), 1);
        assert!(events(&mut run).contains(&RunEvent::AlertRose {
            level: 1,
            cause: AlertCause::Time
        }));
    }

    #[test]
    fn raiders_intercept_a_laden_ship_on_the_return_leg() {
        let (mut run, ship) = quiet(6);
        at_deposit(&mut run, ship);
        run.pilots[0].cargo = [14, 0, 0];
        place(&mut run, ship, (-80.0, 0.0));
        run.step();
        // 14 of 20 is two full thirds.
        assert!(events(&mut run).contains(&RunEvent::Intercepted {
            pilot: ship,
            raiders: 2
        }));
        let raiders = run
            .world
            .ships()
            .iter()
            .filter(|s| s.kind == ShipKind::Raider)
            .count();
        assert_eq!(raiders, 2);
    }

    #[test]
    fn docking_with_enough_cargo_banks_it_and_pays_the_contract() {
        let (mut run, ship) = quiet(7);
        at_deposit(&mut run, ship);
        run.pilots[0].cargo = [12, 0, 0];
        place(&mut run, ship, COMPACT_SITE.dock);
        run.step();
        let summary = events(&mut run).into_iter().find_map(|event| match event {
            RunEvent::Docked { summary, .. } => Some(summary),
            _ => None,
        });
        let summary = summary.expect("docked");
        assert_eq!(summary.banked, [12, 0, 0]);
        assert_eq!(summary.payout, 100);
        assert_eq!(summary.lost, Loadout::default());
    }

    #[test]
    fn a_destroyed_pilot_loses_their_cargo_and_the_run_ends() {
        let (mut run, ship) = quiet(8);
        at_deposit(&mut run, ship);
        run.pilots[0].cargo = [5, 0, 0];
        run.world.damage(ship, 1_000.0);
        run.step();
        let summary = events(&mut run).into_iter().find_map(|event| match event {
            RunEvent::Destroyed { summary, .. } => Some(summary),
            _ => None,
        });
        let summary = summary.expect("destroyed");
        assert_eq!(summary.lost.cargo, 5);
        assert_eq!(summary.banked, [0; 3]);
        assert_eq!(run.ended(), Some(EndReason::AllDone));
    }

    #[test]
    fn a_scan_needs_stillness_and_reveals_the_site() {
        let (mut run, ship) = quiet(9);
        at_deposit(&mut run, ship);
        assert!(run.scan(ship));
        assert!(!run.scan(ship), "already scanning");
        steps(&mut run, SCAN_TICKS / 2);
        run.world.ship_mut(ship).unwrap().state.speed = 5.0;
        run.step();
        assert!(!run.revealed());
        assert!(events(&mut run).contains(&RunEvent::ScanInterrupted { pilot: ship }));
        let deposit = run.site.deposits[0];
        place(&mut run, ship, (deposit.x, deposit.z));
        assert!(run.scan(ship));
        steps(&mut run, SCAN_TICKS);
        assert!(run.revealed());
        assert!(events(&mut run).contains(&RunEvent::Scanned { pilot: ship }));
        assert!(!run.scan(ship), "nothing left to reveal");
    }

    #[test]
    fn a_scan_cannot_start_from_the_dock() {
        let (mut run, ship) = quiet(10);
        assert!(!run.scan(ship));
    }

    #[test]
    fn locks_reach_only_as_far_as_the_lock_range() {
        let (mut run, ship) = quiet(11);
        let near = run.world.spawn_npc(ShipKind::Drone, (-100.0, 0.0));
        let far = run.world.spawn_npc(ShipKind::Drone, (100.0, 0.0));
        assert!(run.set_lock(ship, Some(near)));
        assert!(!run.set_lock(ship, Some(far)));
        assert_eq!(run.world.ship(ship).unwrap().lock, None);
    }

    #[test]
    fn a_lock_holds_while_its_target_strays_beyond_lock_range_and_back() {
        let (mut run, ship) = quiet(15);
        let (dx, dz) = COMPACT_SITE.dock;
        let drone = run.world.spawn_npc(ShipKind::Drone, (dx + 30.0, dz));
        assert!(run.set_lock(ship, Some(drone)));
        // Out past lock range for a while...
        run.world.ship_mut(drone).unwrap().state.x = dx + 200.0;
        run.world.ship_mut(drone).unwrap().home = (dx + 200.0, dz);
        steps(&mut run, 10);
        assert_eq!(run.world.ship(ship).unwrap().lock, Some(drone));
        // ...and back within weapon range: the held trigger fires.
        let near = (dx + 10.0, dz);
        run.world.ship_mut(drone).unwrap().state.x = near.0;
        run.world.ship_mut(drone).unwrap().home = near;
        run.set_controls(ship, ShipInput::default(), true);
        let mut fired = false;
        for _ in 0..20 {
            run.step();
            fired |= run.world.events().iter().any(|event| {
                matches!(event, crate::combat::Event::Fired { shooter, .. } if *shooter == ship)
            });
        }
        assert!(
            fired,
            "the held lock fired once the target was back in range"
        );
    }

    #[test]
    fn the_cruise_drive_triples_speed_on_the_legs_but_not_in_the_site() {
        let (mut run, ship) = quiet(16);
        let cruise = ShipInput {
            thrust: 1.0,
            turn: 0.0,
            cruise: true,
        };
        run.set_controls(ship, cruise, false);
        // The compact site's edge is 65 m out: the top speed comes before it.
        let mut speed: f32 = 0.0;
        for _ in 0..60 {
            run.step();
            if run.status(ship).unwrap().drive == Drive::Ready {
                speed = speed.max(run.world.ship(ship).unwrap().state.speed);
            }
        }
        let top = crate::ship::STARTER_HANDLING.max_speed;
        assert!(speed > top * 2.0, "{speed} m/s on the drive");

        // In the site the drive drops out, and the ship slows to normal speed.
        at_deposit(&mut run, ship);
        run.world.ship_mut(ship).unwrap().state.speed = speed;
        run.set_controls(ship, cruise, false);
        steps(&mut run, 40);
        assert_eq!(run.status(ship).unwrap().drive, Drive::InSite);
        assert!(run.world.ship(ship).unwrap().state.speed <= top + 1e-3);
    }

    #[test]
    fn a_hostile_lock_cuts_the_drive() {
        let (mut run, ship) = quiet(17);
        place(&mut run, ship, (-60.0, 0.0));
        let raider = run.world.spawn_npc(ShipKind::Raider, (-40.0, 0.0));
        run.set_controls(
            ship,
            ShipInput {
                thrust: 1.0,
                turn: 0.0,
                cruise: true,
            },
            false,
        );
        steps(&mut run, 3);
        assert_eq!(run.world.ship(raider).unwrap().lock, Some(ship));
        assert_eq!(run.status(ship).unwrap().drive, Drive::Hunted);
        steps(&mut run, 60);
        let speed = run.world.ship(ship).map_or(0.0, |s| s.state.speed);
        assert!(
            speed <= crate::ship::STARTER_HANDLING.max_speed + 1e-3,
            "{speed}"
        );
    }

    #[test]
    fn hazards_change_heat_and_ranges() {
        let site_with = |hazard| {
            (0..100)
                .map(|seed| Site::generate(COMPACT_SITE, seed, ThreatTier::I, 1, &[]))
                .find(|site| site.values.hazard == hazard)
                .expect("some seed draws each hazard")
        };
        let (fog, _) = Run::launch(site_with(Hazard::IonFog), 1, Options::default());
        assert!((fog.lock_range() - 36.0).abs() < 1e-4);
        assert!((fog.scan_range() - COMPACT_SITE.scan_range * 0.6).abs() < 1e-3);
        let (clear, _) = Run::launch(site_with(Hazard::ThermalFlare), 1, Options::default());
        assert_eq!(clear.lock_range(), LOCK_RANGE);
    }

    #[test]
    fn debris_drift_damages_a_ship_that_hits_rock_between_pockets() {
        let site = (0..200)
            .map(|seed| Site::generate(COMPACT_SITE, seed, ThreatTier::I, 1, &[]))
            .find(|site| {
                site.values.hazard == Hazard::DebrisDrift
                    && site
                        .obstacles
                        .iter()
                        .any(|rock| site.pocket_at(rock.x, rock.z).is_none())
            })
            .expect("a debris-drift site with rock outside its pockets");
        let rock = *site
            .obstacles
            .iter()
            .find(|rock| site.pocket_at(rock.x, rock.z).is_none() && rock.radius > 1.5)
            .or_else(|| {
                site.obstacles
                    .iter()
                    .find(|rock| site.pocket_at(rock.x, rock.z).is_none())
            })
            .unwrap();
        let (mut run, ships) = Run::launch(
            site,
            1,
            Options {
                opening_threat: Some(0),
                ..Options::default()
            },
        );
        let ship = ships[0];
        let start = (rock.x - rock.radius - 3.0, rock.z);
        place(&mut run, ship, start);
        let state = &mut run.world.ship_mut(ship).unwrap().state;
        state.heading = 0.0;
        state.speed = 8.0;
        run.set_controls(
            ship,
            ShipInput {
                thrust: 1.0,
                turn: 0.0,
                cruise: false,
            },
            false,
        );
        steps(&mut run, 10);
        assert!(
            events(&mut run)
                .iter()
                .any(|e| matches!(e, RunEvent::HazardDamage { pilot, .. } if *pilot == ship))
        );
        assert!(run.world.ship(ship).is_none_or(|s| s.hull < 100.0));
    }

    #[test]
    fn leaving_abandons_the_run_and_its_cargo() {
        let (mut run, ship) = quiet(12);
        at_deposit(&mut run, ship);
        run.pilots[0].cargo = [4, 0, 0];
        run.leave(ship);
        assert_eq!(run.ended(), Some(EndReason::AllDone));
        let summary = events(&mut run).into_iter().find_map(|event| match event {
            RunEvent::Abandoned { summary, .. } => Some(summary),
            _ => None,
        });
        assert_eq!(summary.map(|s| (s.banked, s.lost.cargo)), Some(([0; 3], 4)));
    }

    #[test]
    fn the_instance_ends_at_its_limit_with_a_warning() {
        let (mut run, ship) = quiet(13);
        steps(&mut run, INSTANCE_LIMIT_TICKS);
        assert_eq!(run.ended(), Some(EndReason::Limit));
        let log = events(&mut run);
        assert!(log.contains(&RunEvent::LimitWarning));
        assert!(
            log.iter()
                .any(|e| matches!(e, RunEvent::Abandoned { pilot, .. } if *pilot == ship))
        );
    }

    #[test]
    fn a_crew_with_the_capability_can_work_another_pocket() {
        let site = Site::generate(
            COMPACT_SITE,
            14,
            ThreatTier::I,
            1,
            &[Capability::WorkVolatiles],
        );
        let veins = site
            .deposits
            .iter()
            .position(|d| d.material == Material::Volatiles)
            .expect("veins open");
        let (mut run, ships) = Run::launch(
            site,
            1,
            Options {
                opening_threat: Some(0),
                ..Options::default()
            },
        );
        let d = run.site.deposits[veins];
        place(&mut run, ships[0], (d.x, d.z));
        assert_eq!(run.extract(ships[0], true), Ok(()));
    }

    #[test]
    fn a_disconnected_ship_stays_exposed_then_goes_inert_and_comes_back() {
        let (mut run, ship) = quiet(20);
        place(&mut run, ship, (-60.0, 5.0));
        run.disconnect(ship);
        assert!(run.is_disconnected(ship));
        steps(&mut run, EXPOSURE_TICKS - 1);
        assert!(run.world.ship(ship).is_some(), "still exposed");
        steps(&mut run, 2);
        assert!(run.world.ship(ship).is_none(), "held inert, out of reach");
        assert_eq!(
            run.status(ship).unwrap().phase,
            Phase::Approach,
            "not destroyed"
        );
        let drone = run.world.spawn_npc(ShipKind::Drone, (-58.0, 5.0));
        assert!(
            !run.world.set_lock(drone, Some(ship)),
            "an inert ship cannot be locked"
        );
        assert!(run.reconnect(ship));
        let back = run.world.ship(ship).unwrap().state;
        assert!((back.x + 60.0).abs() < 1.0 && (back.z - 5.0).abs() < 1.0);
        assert!(!run.is_disconnected(ship));
        assert!(events(&mut run).contains(&RunEvent::Reconnected { pilot: ship }));
    }

    #[test]
    fn a_ship_in_combat_stays_exposed_for_thirty_seconds() {
        let (mut run, ship) = quiet(21);
        place(&mut run, ship, (-60.0, 0.0));
        run.world.spawn_npc(ShipKind::Raider, (-50.0, 0.0));
        steps(&mut run, 5);
        run.disconnect(ship);
        assert!(events(&mut run).contains(&RunEvent::Disconnected {
            pilot: ship,
            exposed_for: 30
        }));
    }

    #[test]
    fn the_run_waits_for_a_disconnected_pilot_until_the_grace_window_closes() {
        let (mut run, ship) = quiet(22);
        place(&mut run, ship, (-60.0, 0.0));
        run.pilots[0].cargo = [3, 0, 0];
        run.disconnect(ship);
        steps(&mut run, GRACE_TICKS - 1);
        assert_eq!(run.ended(), None, "the run is still theirs to resume");
        steps(&mut run, 2);
        assert_eq!(run.ended(), Some(EndReason::AllDone));
        let summary = events(&mut run).into_iter().find_map(|event| match event {
            RunEvent::Abandoned { summary, .. } => Some(summary),
            _ => None,
        });
        assert_eq!(summary.map(|s| s.lost.cargo), Some(3));
        assert!(!run.reconnect(ship), "too late");
    }

    #[test]
    fn checkpoints_fall_on_entering_a_pocket_and_every_minute() {
        let (mut run, ship) = quiet(23);
        assert!(run.take_checkpoint().is_none());
        at_deposit(&mut run, ship);
        assert!(run.take_checkpoint().is_some(), "entering the ore field");
        assert!(run.take_checkpoint().is_none(), "taken once");
        let until_minute = CHECKPOINT_TICKS - run.elapsed() % CHECKPOINT_TICKS;
        steps(&mut run, until_minute - 1);
        assert!(run.take_checkpoint().is_none());
        run.step();
        assert!(run.take_checkpoint().is_some(), "a minute in");
    }

    #[test]
    fn a_checkpoint_survives_json() {
        let (mut run, ship) = quiet(24);
        at_deposit(&mut run, ship);
        let checkpoint = run.take_checkpoint().unwrap();
        let json = serde_json::to_string(&checkpoint).unwrap();
        assert_eq!(
            serde_json::from_str::<Checkpoint>(&json).unwrap(),
            checkpoint
        );
    }

    #[test]
    fn a_restored_run_has_no_npcs_keeps_its_clock_and_waits_for_its_pilot() {
        let site = Site::generate(COMPACT_SITE, 25, ThreatTier::I, 1, &[]);
        let (mut run, ships) = Run::launch(site, 1, Options::default());
        let ship = ships[0];
        at_deposit(&mut run, ship);
        run.pilots[0].cargo = [6, 0, 0];
        run.extract(ship, true).unwrap();
        // Briefly: the opening threat is in the ore field.
        steps(&mut run, 40);
        let at = run.world.ship(ship).unwrap().state;
        let checkpoint = run.checkpoint();
        assert!(run.world.ships().iter().any(|s| s.kind != ShipKind::Pilot));

        let mut restored = Run::restore(&checkpoint).unwrap();
        assert_eq!(restored.elapsed(), run.elapsed());
        assert_eq!(restored.alert(), run.alert());
        assert_eq!(restored.objective_started(), run.objective_started());
        assert!(
            restored.world.ships().is_empty(),
            "no NPCs, and the ship waits inert"
        );
        assert!(restored.is_disconnected(ship));
        let deposits: Vec<f32> = restored.site.deposits.iter().map(|d| d.remaining).collect();
        assert_eq!(deposits, checkpoint.deposits);

        assert!(restored.reconnect(ship));
        let back = restored.world.ship(ship).unwrap().state;
        assert!((back.x - at.x).abs() < 1e-3 && (back.z - at.z).abs() < 1e-3);
        let status = restored.status(ship).unwrap();
        assert_eq!(status.phase, Phase::Objective);
        assert!(status.cargo >= 6);

        // The alert clock carries on from where it was.
        let next = restored.site.values.rises[0] + restored.objective_started().unwrap();
        let wait = next.saturating_sub(restored.elapsed());
        steps(&mut restored, wait + 1);
        assert_eq!(restored.alert(), 1);
    }

    #[test]
    fn a_checkpoint_of_an_unknown_site_is_not_restored() {
        let (run, _) = quiet(26);
        let mut checkpoint = run.checkpoint();
        checkpoint.template = "Nowhere".into();
        assert!(Run::restore(&checkpoint).is_none());
    }
}
