//! One run of an expedition (design sections 2, 6 and 10): pilots launch from
//! the dock, fly the approach leg, work the site's pockets under rising threat,
//! fly the return leg, and dock to bank their cargo. The instance ends when
//! every pilot has docked, been destroyed or been abandoned, or at its limit.
//!
//! The run keeps an event log of everything that matters to the economy and to
//! the Stage 1 gates; the server writes it out.

use crate::TICKS_PER_SECOND;
use crate::extraction::{
    BASIC_CAPACITY, Conditions, EXTRACT_RANGE, Extractor, ExtractorState, MAX_EXTRACT_SPEED,
    Refusal, Stop,
};
use crate::outcome::{Ending, Loadout, losses};
use crate::profile::ShipKind;
use crate::rng::Rng;
use crate::ship::{ARENA_HALF_WIDTH, ShipInput};
use crate::site::{Hazard, Material, Site, minutes};
use crate::skirmish::MAX_NPCS;
use crate::world::{ShipId, World};

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
/// Impacts slower than this are scrapes and do no damage, m/s.
const MIN_DEBRIS_IMPACT: f32 = 1.0;

/// Where a pilot is in the run.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
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
    Ended {
        reason: EndReason,
    },
}

/// What a pilot's client shows about their own ship and the run.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PilotStatus {
    pub phase: Phase,
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

/// Settings a run is launched with besides its site.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Options {
    /// Replaces the site's opening threat, up to the NPC cap; for benchmarks.
    pub opening_threat: Option<u32>,
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
        };
        run.log(RunEvent::Launched { pilots });
        for &pilot in &ships {
            run.log(RunEvent::SuppliesSpent {
                pilot,
                credits: LAUNCH_COST,
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
        })
    }

    /// Sets a pilot's controls for the next step.
    pub fn set_controls(&mut self, ship: ShipId, input: ShipInput, firing: bool) {
        if self.flying(ship) {
            self.world.set_input(ship, input);
            self.world.set_firing(ship, firing);
        }
    }

    /// Locks a hostile within lock range, or releases the lock. Returns
    /// whether the requested lock was taken.
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

    /// Advances the run by one tick.
    pub fn step(&mut self) {
        if self.ended.is_some() {
            return;
        }
        self.rise_with_time();
        self.world.step();
        self.debris_damage();
        self.record_destroyed();
        for index in 0..self.pilots.len() {
            if self.pilots[index].phase.is_over() {
                continue;
            }
            self.move_pilot(index);
            self.work_extractor(index);
            self.work_scan(index);
            self.keep_lock_in_range(index);
        }
        let elapsed = self.elapsed();
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
            if !pilot.phase.is_over() && self.world.ship(pilot.ship).is_none() {
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

    fn keep_lock_in_range(&mut self, index: usize) {
        let ship = self.pilots[index].ship;
        let lock = self.world.ship(ship).and_then(|s| s.lock);
        if let Some(target) = lock
            && !self.within_lock_range(ship, target)
        {
            self.world.set_lock(ship, None);
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
            launch_cost: LAUNCH_COST,
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
            },
        );
        let d = run.site.deposits[veins];
        place(&mut run, ships[0], (d.x, d.z));
        assert_eq!(run.extract(ships[0], true), Ok(()));
    }
}
