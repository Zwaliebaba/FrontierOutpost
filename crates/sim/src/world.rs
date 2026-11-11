//! The state of one expedition instance.

use crate::Tick;
use crate::collision::{self, Obstacle};
use crate::combat::{Event, MAX_PROJECTILES, Projectile};
use crate::npc;
use crate::profile::ShipKind;
use crate::ship::{self, ShipInput, ShipState};

/// Identifies a ship within one instance. Small, because it crosses the wire.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ShipId(pub u16);

/// A ship: a pilot's or an NPC's.
#[derive(Debug, Clone, PartialEq)]
pub struct Ship {
    pub id: ShipId,
    pub kind: ShipKind,
    pub state: ShipState,
    /// What it is flying with this tick: a pilot's controls, or an NPC's choice.
    pub input: ShipInput,
    pub hull: f32,
    /// Cargo units carried, as the run last set them.
    pub cargo: u32,
    /// Where an NPC patrols, or a warden's post. A pilot's spawn point.
    pub home: (f32, f32),
    /// The ship this one has locked: chosen by its pilot, or by its NPC logic.
    pub lock: Option<ShipId>,
    /// Whether it is trying to fire at its lock: its pilot holds the trigger,
    /// or its NPC logic has the lock in range.
    pub firing: bool,
    /// Steps until its weapon can fire again.
    pub cooldown: u32,
    /// Whether its extractor is working; drones gather on extracting ships.
    pub extracting: bool,
    /// What its fittings change in flight.
    pub kit: Kit,
    /// Shield points left, absorbed before the hull.
    pub shield: f32,
    /// Steps since it was last hit, for shield regeneration.
    pub since_hit: u32,
    /// Hits count only from shooters inside this circle, `(x, z, radius)`:
    /// the nest core can be harmed only from within its pocket.
    pub vulnerable_from: Option<(f32, f32, f32)>,
    /// Steps until its point defence can intercept again.
    pub defence_cooldown: u32,
}

/// What a ship's fittings change in flight (design section 10's rigs and
/// modules). Placeholders where the design gives only words.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Kit {
    /// Multiplies top speed and acceleration.
    pub speed: f32,
    /// Multiplies weapon damage.
    pub weapon: f32,
    /// Shield points when full.
    pub shield: f32,
    /// Intercepts projectiles near the ship.
    pub point_defence: bool,
}

impl Kit {
    pub const BASIC: Self = Self {
        speed: 1.0,
        weapon: 1.0,
        shield: 0.0,
        point_defence: false,
    };
}

impl Default for Kit {
    fn default() -> Self {
        Self::BASIC
    }
}

/// A shield starts to recover this long after its last hit. A placeholder.
pub const SHIELD_DELAY_TICKS: u32 = 5 * crate::TICKS_PER_SECOND;
/// Shield points recovered per second once it recovers. A placeholder.
pub const SHIELD_REGEN: f32 = 8.0;
/// Point defence intercepts shots this close to the ship, metres. A placeholder.
pub const POINT_DEFENCE_RANGE: f32 = 8.0;
/// Point defence intercepts at most one shot this often. A placeholder.
pub const POINT_DEFENCE_COOLDOWN_TICKS: u32 = crate::TICKS_PER_SECOND;

/// A ship's handling with its kit applied.
#[must_use]
pub fn handling(kind: ShipKind, kit: &Kit) -> ship::Handling {
    let base = kind.profile().handling;
    ship::Handling {
        max_speed: base.max_speed * kit.speed,
        acceleration: base.acceleration * kit.speed,
        ..base
    }
}

impl Ship {
    #[must_use]
    pub fn new(id: ShipId, kind: ShipKind, state: ShipState) -> Self {
        Self {
            id,
            kind,
            state,
            input: ShipInput::default(),
            hull: kind.profile().max_hull,
            cargo: 0,
            home: (state.x, state.z),
            lock: None,
            firing: false,
            cooldown: 0,
            extracting: false,
            kit: Kit::BASIC,
            shield: 0.0,
            since_hit: 0,
            vulnerable_from: None,
            defence_cooldown: 0,
        }
    }
}

/// Every entity in an instance, advanced one tick at a time.
#[derive(Debug, Clone, Default)]
pub struct World {
    tick: Tick,
    ships: Vec<Ship>,
    obstacles: Vec<Obstacle>,
    projectiles: Vec<Projectile>,
    /// What happened during the latest step.
    events: Vec<Event>,
    /// Ships that hit an obstacle during the latest step, and how hard, m/s.
    impacts: Vec<(ShipId, f32)>,
    next_id: u16,
}

/// Spawn points on a ring around the origin, facing outward, so ships that
/// join together do not start on top of one another.
const SPAWN_RADIUS: f32 = 6.0;
const SPAWN_SLOTS: u16 = 8;

/// Which spawn slot a pilot's home is, if it is one.
fn spawn_slot(home: (f32, f32)) -> Option<u16> {
    (0..SPAWN_SLOTS).find(|slot| {
        let angle = f32::from(*slot) * std::f32::consts::TAU / f32::from(SPAWN_SLOTS);
        (home.0 - angle.cos() * SPAWN_RADIUS).hypot(home.1 - angle.sin() * SPAWN_RADIUS) < 0.01
    })
}

impl World {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// A world with these fixed obstacles.
    #[must_use]
    pub fn with_obstacles(obstacles: Vec<Obstacle>) -> Self {
        Self {
            obstacles,
            ..Self::default()
        }
    }

    #[must_use]
    pub fn obstacles(&self) -> &[Obstacle] {
        &self.obstacles
    }

    #[must_use]
    pub fn tick(&self) -> Tick {
        self.tick
    }

    #[must_use]
    pub fn ships(&self) -> &[Ship] {
        &self.ships
    }

    #[must_use]
    pub fn ship(&self, id: ShipId) -> Option<&Ship> {
        self.ships.iter().find(|ship| ship.id == id)
    }

    #[must_use]
    pub fn projectiles(&self) -> &[Projectile] {
        &self.projectiles
    }

    /// What happened during the latest step.
    #[must_use]
    pub fn events(&self) -> &[Event] {
        &self.events
    }

    /// Holds or releases a pilot's trigger. Returns whether the ship exists.
    pub fn set_firing(&mut self, id: ShipId, firing: bool) -> bool {
        match self.ships.iter_mut().find(|ship| ship.id == id) {
            Some(ship) => {
                ship.firing = firing;
                true
            }
            None => false,
        }
    }

    fn next_id(&mut self) -> ShipId {
        let id = ShipId(self.next_id);
        self.next_id = self.next_id.wrapping_add(1);
        id
    }

    /// Adds a pilot's ship at the next free spawn point and returns its id.
    pub fn spawn_ship(&mut self) -> ShipId {
        let id = self.next_id();
        let taken = |slot: u16| {
            self.ships
                .iter()
                .any(|ship| ship.kind == ShipKind::Pilot && spawn_slot(ship.home) == Some(slot))
        };
        let slot = (0..SPAWN_SLOTS)
            .find(|slot| !taken(*slot))
            .unwrap_or(id.0 % SPAWN_SLOTS);
        let angle = f32::from(slot) * std::f32::consts::TAU / f32::from(SPAWN_SLOTS);
        let state = ShipState {
            x: angle.cos() * SPAWN_RADIUS,
            z: angle.sin() * SPAWN_RADIUS,
            heading: angle,
            speed: 0.0,
        };
        self.ships.push(Ship::new(id, ShipKind::Pilot, state));
        id
    }

    /// Adds a pilot's ship at `home`, facing `heading`, and returns its id.
    pub fn spawn_pilot_at(&mut self, home: (f32, f32), heading: f32) -> ShipId {
        let id = self.next_id();
        let state = ShipState {
            x: home.0,
            z: home.1,
            heading,
            speed: 0.0,
        };
        self.ships.push(Ship::new(id, ShipKind::Pilot, state));
        id
    }

    /// Puts a pilot's ship back under its old id, as it was saved: for a
    /// pilot who reconnects, or a run restored after a restart.
    pub fn restore_pilot(&mut self, id: ShipId, state: ShipState, hull: f32) {
        self.remove_ship(id);
        let mut ship = Ship::new(id, ShipKind::Pilot, state);
        ship.hull = hull;
        self.ships.push(ship);
        if self.next_id <= id.0 {
            self.next_id = id.0.wrapping_add(1);
        }
    }

    /// Keeps `id` from being given to a new ship: for a pilot's ship that is
    /// out of the world and will be put back under its old id.
    pub fn reserve(&mut self, id: ShipId) {
        if self.next_id <= id.0 {
            self.next_id = id.0.wrapping_add(1);
        }
    }

    /// Starts the clock at `tick`, for a run restored from a checkpoint.
    pub fn set_tick(&mut self, tick: Tick) {
        self.tick = tick;
    }

    /// Adds an NPC at `at` that patrols or guards `home`, and returns its id.
    pub fn spawn_npc_at(&mut self, kind: ShipKind, at: (f32, f32), home: (f32, f32)) -> ShipId {
        let id = self.spawn_npc(kind, home);
        if let Some(ship) = self.ships.iter_mut().find(|ship| ship.id == id) {
            ship.state.x = at.0;
            ship.state.z = at.1;
        }
        id
    }

    /// A ship to change directly, for tests that place ships.
    #[cfg(test)]
    pub(crate) fn ship_mut(&mut self, id: ShipId) -> Option<&mut Ship> {
        self.ships.iter_mut().find(|ship| ship.id == id)
    }

    /// Sets the cargo a ship carries. Returns whether it exists.
    pub fn set_cargo(&mut self, id: ShipId, cargo: u32) -> bool {
        match self.ships.iter_mut().find(|ship| ship.id == id) {
            Some(ship) => {
                ship.cargo = cargo;
                true
            }
            None => false,
        }
    }

    /// Marks whether a ship's extractor is working. Returns whether it exists.
    pub fn set_extracting(&mut self, id: ShipId, extracting: bool) -> bool {
        match self.ships.iter_mut().find(|ship| ship.id == id) {
            Some(ship) => {
                ship.extracting = extracting;
                true
            }
            None => false,
        }
    }

    /// Fits a ship with `kit`, its shield full. Returns whether it exists.
    pub fn set_kit(&mut self, id: ShipId, kit: Kit) -> bool {
        match self.ships.iter_mut().find(|ship| ship.id == id) {
            Some(ship) => {
                ship.kit = kit;
                ship.shield = kit.shield;
                true
            }
            None => false,
        }
    }

    /// Lets hits on a ship count only from shooters inside `circle`, `(x, z,
    /// radius)`. Returns whether it exists.
    pub fn set_vulnerable_from(&mut self, id: ShipId, circle: Option<(f32, f32, f32)>) -> bool {
        match self.ships.iter_mut().find(|ship| ship.id == id) {
            Some(ship) => {
                ship.vulnerable_from = circle;
                true
            }
            None => false,
        }
    }

    /// Sets a ship's hull, for a structure restored from a checkpoint.
    pub fn set_hull(&mut self, id: ShipId, hull: f32) -> bool {
        match self.ships.iter_mut().find(|ship| ship.id == id) {
            Some(ship) => {
                ship.hull = hull;
                true
            }
            None => false,
        }
    }

    /// Damages a ship from outside combat, such as a hazard, destroying it if
    /// its hull runs out. Returns whether it was destroyed.
    pub fn damage(&mut self, id: ShipId, amount: f32) -> bool {
        let Some(ship) = self.ships.iter_mut().find(|ship| ship.id == id) else {
            return false;
        };
        let was_alive = ship.hull > 0.0;
        ship.hull -= amount;
        if !(was_alive && ship.hull <= 0.0) {
            return false;
        }
        let kind = ship.kind;
        self.remove_ship(id);
        self.events.push(Event::Destroyed {
            ship: id,
            kind,
            by: None,
        });
        true
    }

    /// Ships that hit an obstacle during the latest step, and how hard, m/s.
    #[must_use]
    pub fn impacts(&self) -> &[(ShipId, f32)] {
        &self.impacts
    }

    /// Adds an NPC at `home` and returns its id.
    pub fn spawn_npc(&mut self, kind: ShipKind, home: (f32, f32)) -> ShipId {
        let id = self.next_id();
        let state = ShipState {
            x: home.0,
            z: home.1,
            heading: 0.0,
            speed: 0.0,
        };
        self.ships.push(Ship::new(id, kind, state));
        id
    }

    /// Removes a ship and releases every lock on it. Returns whether it existed.
    pub fn remove_ship(&mut self, id: ShipId) -> bool {
        let before = self.ships.len();
        self.ships.retain(|ship| ship.id != id);
        for ship in &mut self.ships {
            if ship.lock == Some(id) {
                ship.lock = None;
            }
        }
        self.ships.len() != before
    }

    /// Locks `target` for the ship `id`, or releases its lock with `None`. A
    /// ship can lock only an existing ship hostile to it; anything else
    /// releases the lock. Returns whether the requested lock was taken.
    pub fn set_lock(&mut self, id: ShipId, target: Option<ShipId>) -> bool {
        let Some(kind) = self.ship(id).map(|ship| ship.kind) else {
            return false;
        };
        let valid = target.filter(|target| {
            self.ship(*target)
                .is_some_and(|other| kind.is_hostile_to(other.kind))
        });
        if let Some(ship) = self.ships.iter_mut().find(|ship| ship.id == id) {
            ship.lock = valid;
        }
        valid == target
    }

    /// Sets the input a ship flies with from the next step on. Returns whether
    /// the ship exists.
    pub fn set_input(&mut self, id: ShipId, input: ShipInput) -> bool {
        match self.ships.iter_mut().find(|ship| ship.id == id) {
            Some(ship) => {
                ship.input = input;
                true
            }
            None => false,
        }
    }

    /// Advances the world by one step: NPCs decide, every ship moves, then
    /// collisions resolve. Ships push one another apart, and obstacles go last,
    /// so no ship ends a step inside one.
    pub fn step(&mut self) {
        self.events.clear();
        self.think();
        for ship in &mut self.ships {
            let handling = handling(ship.kind, &ship.kit);
            ship.state = ship::step(ship.state, ship.input, &handling);
            ship.since_hit = ship.since_hit.saturating_add(1);
            if ship.since_hit >= SHIELD_DELAY_TICKS {
                ship.shield = (ship.shield + SHIELD_REGEN * ship::DT).min(ship.kit.shield);
            }
            ship.defence_cooldown = ship.defence_cooldown.saturating_sub(1);
        }
        for i in 0..self.ships.len() {
            let (done, rest) = self.ships.split_at_mut(i + 1);
            let Some(a) = done.last_mut() else { continue };
            for b in rest {
                let (ra, rb) = (a.kind.profile().radius, b.kind.profile().radius);
                collision::separate(&mut a.state, ra, &mut b.state, rb);
            }
        }
        self.impacts.clear();
        for ship in &mut self.ships {
            let radius = ship.kind.profile().radius;
            let (state, impact) =
                collision::resolve_obstacles_with_impact(ship.state, radius, &self.obstacles);
            ship.state = state;
            if impact > 0.0 {
                self.impacts.push((ship.id, impact));
            }
        }
        self.fire();
        self.fly_projectiles();
        self.tick = self.tick.next();
    }

    /// Fires every weapon that is ready, held and has a hostile lock in range.
    fn fire(&mut self) {
        let mut shots = Vec::new();
        for ship in &self.ships {
            let weapon = ship.kind.profile().weapon;
            let Some(target) = ship.lock.and_then(|id| self.ship(id)) else {
                continue;
            };
            let distance = (target.state.x - ship.state.x).hypot(target.state.z - ship.state.z);
            if ship.firing
                && ship.cooldown == 0
                && ship.kind.is_hostile_to(target.kind)
                && distance <= weapon.range
            {
                shots.push((ship.id, target.id));
            }
        }
        for ship in &mut self.ships {
            ship.cooldown = ship.cooldown.saturating_sub(1);
        }
        for (shooter_id, target) in shots {
            if self.projectiles.len() >= MAX_PROJECTILES {
                break;
            }
            let Some(shooter) = self.ships.iter_mut().find(|ship| ship.id == shooter_id) else {
                continue;
            };
            let weapon = shooter.kind.profile().weapon;
            let damage = weapon.damage * shooter.kit.weapon;
            shooter.cooldown = weapon.cooldown_ticks;
            // Long enough to cross twice the weapon's range, then it gives up.
            let flight = 2.0 * weapon.range / (weapon.projectile_speed * ship::DT);
            self.projectiles.push(Projectile {
                shooter: shooter_id,
                target,
                x: shooter.state.x,
                z: shooter.state.z,
                speed: weapon.projectile_speed,
                damage,
                // A small positive float, so `as` cannot lose anything that matters.
                ticks_left: flight.ceil() as u32 + 1,
            });
            self.events.push(Event::Fired {
                shooter: shooter_id,
                target,
                speed: weapon.projectile_speed,
            });
        }
    }

    /// Intercepts shots that close on a ship with point defence: one per
    /// cooldown, nearest first.
    fn defend(&mut self) {
        let mut intercepted = Vec::new();
        for ship in &mut self.ships {
            if !ship.kit.point_defence || ship.defence_cooldown > 0 {
                continue;
            }
            let nearest = self
                .projectiles
                .iter()
                .enumerate()
                .filter(|(_, p)| p.target == ship.id)
                .map(|(i, p)| (i, (p.x - ship.state.x).hypot(p.z - ship.state.z)))
                .filter(|&(_, distance)| distance <= POINT_DEFENCE_RANGE)
                .min_by(|a, b| a.1.total_cmp(&b.1));
            if let Some((index, _)) = nearest {
                ship.defence_cooldown = POINT_DEFENCE_COOLDOWN_TICKS;
                intercepted.push(index);
            }
        }
        intercepted.sort_unstable();
        for index in intercepted.into_iter().rev() {
            let shot = self.projectiles.remove(index);
            self.events.push(Event::Intercepted {
                shooter: shot.shooter,
                target: shot.target,
            });
        }
    }

    /// Moves every projectile toward its target, applying hits and removing
    /// ships whose hull runs out.
    fn fly_projectiles(&mut self) {
        self.defend();
        let mut hits = Vec::new();
        self.projectiles.retain_mut(|projectile| {
            let Some(target) = self.ships.iter().find(|ship| ship.id == projectile.target) else {
                // Its target is gone: the shot is wasted.
                return false;
            };
            let (dx, dz) = (target.state.x - projectile.x, target.state.z - projectile.z);
            let distance = dx.hypot(dz);
            let travel = projectile.speed * ship::DT;
            if distance <= travel + target.kind.profile().radius {
                hits.push((projectile.target, projectile.shooter, projectile.damage));
                return false;
            }
            projectile.x += dx / distance * travel;
            projectile.z += dz / distance * travel;
            projectile.ticks_left = projectile.ticks_left.saturating_sub(1);
            projectile.ticks_left > 0
        });

        let mut destroyed = Vec::new();
        for (target, shooter, damage) in hits {
            let from = self.ship(shooter).map(|ship| (ship.state.x, ship.state.z));
            let Some(ship) = self.ships.iter_mut().find(|ship| ship.id == target) else {
                continue;
            };
            if let Some((cx, cz, radius)) = ship.vulnerable_from {
                let inside = from.is_some_and(|(x, z)| (x - cx).hypot(z - cz) <= radius);
                if !inside {
                    continue;
                }
            }
            let was_alive = ship.hull > 0.0;
            let absorbed = ship.shield.min(damage);
            ship.shield -= absorbed;
            ship.hull -= damage - absorbed;
            ship.since_hit = 0;
            if was_alive && ship.hull <= 0.0 {
                destroyed.push((ship.id, ship.kind, shooter));
            }
        }
        for (ship, kind, by) in destroyed {
            self.remove_ship(ship);
            self.events.push(Event::Destroyed {
                ship,
                kind,
                by: Some(by),
            });
        }
    }

    /// Lets every NPC choose a target and how to steer.
    fn think(&mut self) {
        let tick = self.tick.get();
        let decisions: Vec<(usize, Option<ShipId>, ShipInput)> = self
            .ships
            .iter()
            .enumerate()
            .filter(|(_, ship)| ship.kind.thinks())
            .map(|(index, npc)| {
                let target = npc::choose_target(npc, &self.ships);
                let target_ship = target.and_then(|id| self.ship(id));
                (index, target, npc::steer(npc, target_ship, tick))
            })
            .collect();
        for (index, target, input) in decisions {
            let in_range = target
                .and_then(|id| self.ship(id))
                .zip(self.ships.get(index))
                .is_some_and(|(target, npc)| {
                    let distance =
                        (target.state.x - npc.state.x).hypot(target.state.z - npc.state.z);
                    distance <= npc.kind.profile().weapon.range
                });
            if let Some(npc) = self.ships.get_mut(index) {
                npc.lock = target;
                npc.input = input;
                npc.firing = in_range;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A pilot and a drone 5 m apart; the drone engages the pilot.
    fn drone_on_pilot() -> (World, ShipId, ShipId) {
        let mut world = World::new();
        let pilot = world.spawn_pilot_at((0.0, 0.0), 0.0);
        let drone = world.spawn_npc_at(ShipKind::Drone, (5.0, 0.0), (5.0, 0.0));
        (world, pilot, drone)
    }

    #[test]
    fn a_shield_takes_hits_before_the_hull_and_recovers_out_of_combat() {
        let mut world = World::new();
        let pilot = world.spawn_pilot_at((0.0, 0.0), 0.0);
        let raider = world.spawn_pilot_at((10.0, 0.0), 0.0);
        world.ship_mut(raider).unwrap().kind = ShipKind::Raider;
        world.set_kit(
            pilot,
            Kit {
                shield: 40.0,
                ..Kit::BASIC
            },
        );
        world.projectiles.push(Projectile {
            shooter: raider,
            target: pilot,
            x: 0.5,
            z: 0.0,
            speed: 30.0,
            damage: 50.0,
            ticks_left: 10,
        });
        world.remove_ship(raider);
        world.step();
        let ship = world.ship(pilot).unwrap();
        assert!(ship.shield.abs() < f32::EPSILON);
        assert!((ship.hull - 90.0).abs() < f32::EPSILON, "{}", ship.hull);
        for _ in 0..SHIELD_DELAY_TICKS + 5 * crate::TICKS_PER_SECOND {
            world.step();
        }
        assert!((world.ship(pilot).unwrap().shield - 40.0).abs() < f32::EPSILON);
    }

    #[test]
    fn point_defence_intercepts_one_shot_a_second() {
        let (mut world, pilot, drone) = drone_on_pilot();
        world.set_kit(
            pilot,
            Kit {
                point_defence: true,
                ..Kit::BASIC
            },
        );
        for _ in 0..crate::TICKS_PER_SECOND * 3 {
            world.step();
        }
        let hull = world.ship(pilot).unwrap().hull;
        let (mut unguarded, plain, _) = drone_on_pilot();
        for _ in 0..crate::TICKS_PER_SECOND * 3 {
            unguarded.step();
        }
        let unguarded_hull = unguarded.ship(plain).unwrap().hull;
        assert!(hull > unguarded_hull, "{hull} against {unguarded_hull}");
        assert!(world.ship(drone).is_some());
    }

    #[test]
    fn a_structure_ignores_shots_from_outside_its_circle() {
        let mut world = World::new();
        let pilot = world.spawn_pilot_at((0.0, 0.0), 0.0);
        let core = world.spawn_npc_at(ShipKind::Core, (10.0, 0.0), (10.0, 0.0));
        world.set_vulnerable_from(core, Some((50.0, 0.0, 5.0)));
        world.set_lock(pilot, Some(core));
        world.set_firing(pilot, true);
        for _ in 0..crate::TICKS_PER_SECOND * 2 {
            world.step();
        }
        let full = crate::profile::CORE_HULL;
        assert!((world.ship(core).unwrap().hull - full).abs() < f32::EPSILON);
        world.set_vulnerable_from(core, Some((0.0, 0.0, 5.0)));
        for _ in 0..crate::TICKS_PER_SECOND * 2 {
            world.step();
        }
        assert!(world.ship(core).unwrap().hull < full);
        assert_eq!(
            world.ship(core).unwrap().state.x,
            10.0,
            "a structure never moves"
        );
    }

    #[test]
    fn a_kit_doubles_damage_and_speeds_the_ship() {
        let mut world = World::new();
        let pilot = world.spawn_pilot_at((0.0, 0.0), 0.0);
        let core = world.spawn_npc_at(ShipKind::Core, (10.0, 0.0), (10.0, 0.0));
        world.set_kit(
            pilot,
            Kit {
                weapon: 2.0,
                speed: 1.2,
                ..Kit::BASIC
            },
        );
        world.set_lock(pilot, Some(core));
        world.set_firing(pilot, true);
        world.step();
        assert!((world.projectiles()[0].damage - 20.0).abs() < f32::EPSILON);
        let fast = handling(ShipKind::Pilot, &world.ship(pilot).unwrap().kit);
        assert!((fast.max_speed - 9.0 * 1.2).abs() < 1e-5);
    }

    #[test]
    fn spawned_ships_get_distinct_ids_and_positions() {
        let mut world = World::new();
        let a = world.spawn_ship();
        let b = world.spawn_ship();
        assert_ne!(a, b);
        let (sa, sb) = (world.ship(a).unwrap().state, world.ship(b).unwrap().state);
        assert!((sa.x - sb.x).hypot(sa.z - sb.z) > 1.0);
    }

    #[test]
    fn stepping_advances_the_tick_and_moves_ships_with_their_input() {
        let mut world = World::new();
        let moving = world.spawn_ship();
        let idle = world.spawn_ship();
        assert!(world.set_input(
            moving,
            ShipInput {
                thrust: 1.0,
                turn: 0.0,
                cruise: false,
            }
        ));
        let before = (
            world.ship(moving).unwrap().state,
            world.ship(idle).unwrap().state,
        );
        for _ in 0..10 {
            world.step();
        }
        assert_eq!(world.tick(), Tick::new(10));
        assert_ne!(world.ship(moving).unwrap().state, before.0);
        assert_eq!(world.ship(idle).unwrap().state, before.1);
    }

    #[test]
    fn removed_ships_are_gone_and_ignore_input() {
        let mut world = World::new();
        let id = world.spawn_ship();
        assert!(world.remove_ship(id));
        assert!(!world.remove_ship(id));
        assert!(world.ship(id).is_none());
        assert!(!world.set_input(id, ShipInput::default()));
    }

    #[test]
    fn ships_flying_into_each_other_do_not_overlap() {
        let mut world = World::new();
        let a = world.spawn_ship();
        let b = world.spawn_ship();
        // Put them nose to nose and thrust into each other.
        world.ships[0].state = ShipState {
            x: -3.0,
            z: 0.0,
            heading: 0.0,
            speed: 9.0,
        };
        world.ships[1].state = ShipState {
            x: 3.0,
            z: 0.0,
            heading: std::f32::consts::PI,
            speed: 9.0,
        };
        let thrust = ShipInput {
            thrust: 1.0,
            turn: 0.0,
            cruise: false,
        };
        world.set_input(a, thrust);
        world.set_input(b, thrust);
        for _ in 0..40 {
            world.step();
            let (sa, sb) = (world.ship(a).unwrap().state, world.ship(b).unwrap().state);
            assert!((sa.x - sb.x).hypot(sa.z - sb.z) >= 2.0 * crate::collision::SHIP_RADIUS - 1e-3);
        }
    }

    #[test]
    fn a_ship_cannot_fly_through_an_obstacle() {
        let rock = Obstacle {
            x: 10.0,
            z: 0.0,
            radius: 3.0,
        };
        let mut world = World::with_obstacles(vec![rock]);
        let id = world.spawn_ship();
        world.ships[0].state = ShipState::default();
        world.set_input(
            id,
            ShipInput {
                thrust: 1.0,
                turn: 0.0,
                cruise: false,
            },
        );
        for _ in 0..200 {
            world.step();
            let state = world.ship(id).unwrap().state;
            assert!(
                (state.x - rock.x).hypot(state.z)
                    >= rock.radius + crate::collision::SHIP_RADIUS - 1e-3
            );
        }
        assert!(world.ship(id).unwrap().state.x < rock.x);
    }

    #[test]
    fn a_pilot_gets_a_free_spawn_point_after_others_leave() {
        let mut world = World::new();
        let a = world.spawn_ship();
        let b = world.spawn_ship();
        world.remove_ship(a);
        let c = world.spawn_ship();
        let (sb, sc) = (world.ship(b).unwrap().state, world.ship(c).unwrap().state);
        assert!((sb.x - sc.x).hypot(sb.z - sc.z) > 1.0);
    }

    #[test]
    fn npcs_lock_and_chase_a_pilot_they_notice() {
        let mut world = World::new();
        let pilot = world.spawn_ship();
        let drone = world.spawn_npc(ShipKind::Drone, (30.0, 0.0));
        let start = world.ship(drone).unwrap().state;
        for _ in 0..40 {
            world.step();
        }
        let after = world.ship(drone).unwrap();
        assert_eq!(after.lock, Some(pilot));
        let (p, d0, d1) = (world.ship(pilot).unwrap().state, start, after.state);
        assert!((d1.x - p.x).hypot(d1.z - p.z) < (d0.x - p.x).hypot(d0.z - p.z));
    }

    #[test]
    fn npcs_patrol_near_home_when_no_pilot_is_near() {
        let mut world = World::new();
        let raider = world.spawn_npc(ShipKind::Raider, (100.0, 100.0));
        for _ in 0..600 {
            world.step();
        }
        let raider = world.ship(raider).unwrap();
        assert_eq!(raider.lock, None);
        assert!((raider.state.x - 100.0).hypot(raider.state.z - 100.0) < 25.0);
    }

    #[test]
    fn a_pilot_can_lock_only_an_existing_hostile() {
        let mut world = World::new();
        let pilot = world.spawn_ship();
        let ally = world.spawn_ship();
        let drone = world.spawn_npc(ShipKind::Drone, (50.0, 0.0));
        assert!(world.set_lock(pilot, Some(drone)));
        assert_eq!(world.ship(pilot).unwrap().lock, Some(drone));
        assert!(!world.set_lock(pilot, Some(ally)));
        assert_eq!(
            world.ship(pilot).unwrap().lock,
            None,
            "an invalid lock releases it"
        );
        assert!(!world.set_lock(pilot, Some(ShipId(999))));
        assert!(world.set_lock(pilot, None));
    }

    #[test]
    fn removing_a_ship_releases_locks_on_it() {
        let mut world = World::new();
        let pilot = world.spawn_ship();
        let drone = world.spawn_npc(ShipKind::Drone, (50.0, 0.0));
        world.set_lock(pilot, Some(drone));
        world.remove_ship(drone);
        assert_eq!(world.ship(pilot).unwrap().lock, None);
    }

    /// A pilot at the origin facing a stationary `kind` at `distance` along +x,
    /// locked and holding the trigger.
    fn duel(kind: ShipKind, distance: f32) -> (World, ShipId, ShipId) {
        let mut world = World::new();
        let pilot = world.spawn_ship();
        let target = world.spawn_npc(kind, (distance, 0.0));
        world.ships[0].state = ShipState::default();
        world.set_lock(pilot, Some(target));
        world.set_firing(pilot, true);
        (world, pilot, target)
    }

    fn fired(world: &World, shooter: ShipId) -> bool {
        world
            .events()
            .iter()
            .any(|event| matches!(event, Event::Fired { shooter: s, .. } if *s == shooter))
    }

    #[test]
    fn fires_only_within_range() {
        let range = ShipKind::Pilot.profile().weapon.range;
        let (mut world, pilot, _) = duel(ShipKind::Warden, range + 30.0);
        // Hold the warden still and out of reach of its own leash logic.
        world.step();
        assert!(!fired(&world, pilot), "out of range");
        let (mut world, pilot, _) = duel(ShipKind::Warden, range - 1.0);
        world.step();
        assert!(fired(&world, pilot), "in range");
    }

    #[test]
    fn fires_once_per_cooldown_while_the_trigger_is_held() {
        let (mut world, pilot, _) = duel(ShipKind::Warden, 10.0);
        let cooldown = ShipKind::Pilot.profile().weapon.cooldown_ticks as usize;
        let mut shots = 0;
        for _ in 0..cooldown * 3 {
            world.step();
            shots += usize::from(fired(&world, pilot));
        }
        assert_eq!(shots, 3);
    }

    #[test]
    fn needs_the_trigger_and_a_hostile_lock() {
        let (mut world, pilot, _) = duel(ShipKind::Warden, 10.0);
        world.set_firing(pilot, false);
        world.step();
        assert!(!fired(&world, pilot), "trigger released");
        world.set_firing(pilot, true);
        world.set_lock(pilot, None);
        world.step();
        assert!(!fired(&world, pilot), "nothing locked");
    }

    #[test]
    fn a_hit_removes_the_weapon_damage_from_the_target() {
        let (mut world, pilot, warden) = duel(ShipKind::Warden, 10.0);
        let full = ShipKind::Warden.profile().max_hull;
        world.step();
        world.set_firing(pilot, false);
        for _ in 0..20 {
            world.step();
        }
        let damage = ShipKind::Pilot.profile().weapon.damage;
        assert!((world.ship(warden).unwrap().hull - (full - damage)).abs() < 1e-4);
        assert!(world.projectiles().is_empty());
    }

    #[test]
    fn a_ship_whose_hull_runs_out_is_destroyed_and_reported() {
        let (mut world, pilot, drone) = duel(ShipKind::Drone, 6.0);
        let shots = (ShipKind::Drone.profile().max_hull / ShipKind::Pilot.profile().weapon.damage)
            .ceil() as usize;
        let cooldown = ShipKind::Pilot.profile().weapon.cooldown_ticks as usize;
        let mut destroyed = None;
        for _ in 0..(shots + 1) * cooldown + 20 {
            // Keep the pilot alive and in place for the test.
            world.ships[0].hull = 100.0;
            world.step();
            for event in world.events() {
                if let Event::Destroyed { ship, by, .. } = event {
                    destroyed = Some((*ship, *by));
                }
            }
            if destroyed.is_some() {
                break;
            }
        }
        assert_eq!(destroyed, Some((drone, Some(pilot))));
        assert!(world.ship(drone).is_none());
        assert_eq!(
            world.ship(pilot).unwrap().lock,
            None,
            "the lock is released"
        );
    }

    #[test]
    fn a_shot_at_a_ship_that_is_gone_is_wasted() {
        let (mut world, pilot, warden) = duel(ShipKind::Warden, 15.0);
        let pilots_shots = |world: &World| {
            world
                .projectiles()
                .iter()
                .filter(|p| p.shooter == pilot)
                .count()
        };
        world.step();
        assert_eq!(pilots_shots(&world), 1);
        world.remove_ship(warden);
        world.step();
        assert_eq!(pilots_shots(&world), 0);
    }

    #[test]
    fn never_more_than_the_projectile_cap_in_flight() {
        let mut world = World::new();
        let target = world.spawn_npc(ShipKind::Warden, (0.0, 0.0));
        world.ships[0].hull = f32::MAX;
        for i in 0..100 {
            let pilot = world.spawn_ship();
            let angle = i as f32 * 0.0628;
            world.ships.last_mut().unwrap().state = ShipState {
                x: angle.cos() * 20.0,
                z: angle.sin() * 20.0,
                heading: 0.0,
                speed: 0.0,
            };
            world.set_lock(pilot, Some(target));
            world.set_firing(pilot, true);
        }
        for _ in 0..3 {
            world.step();
            assert!(world.projectiles().len() <= MAX_PROJECTILES);
        }
        assert_eq!(world.projectiles().len(), MAX_PROJECTILES);
    }

    #[test]
    fn an_npc_fires_at_a_pilot_in_its_range() {
        let mut world = World::new();
        let pilot = world.spawn_ship();
        let drone = world.spawn_npc(ShipKind::Drone, (8.0, 0.0));
        let mut shot_at = false;
        for _ in 0..40 {
            world.step();
            shot_at |= world.events().iter().any(|event| {
                matches!(event, Event::Fired { shooter, target, .. } if *shooter == drone && *target == pilot)
            });
        }
        assert!(shot_at);
        assert!(world.ship(pilot).unwrap().hull < ShipKind::Pilot.profile().max_hull);
    }

    #[test]
    fn ids_are_not_reused_after_removal() {
        let mut world = World::new();
        let first = world.spawn_ship();
        world.remove_ship(first);
        assert_ne!(world.spawn_ship(), first);
    }

    #[test]
    fn damage_from_outside_combat_destroys_and_reports() {
        let mut world = World::new();
        let pilot = world.spawn_pilot_at((5.0, 5.0), 1.0);
        assert_eq!(world.ship(pilot).unwrap().state.heading, 1.0);
        assert!(!world.damage(pilot, 40.0));
        assert!(world.damage(pilot, 70.0));
        assert!(world.ship(pilot).is_none());
        assert!(matches!(
            world.events(),
            [Event::Destroyed {
                by: None,
                kind: ShipKind::Pilot,
                ..
            }]
        ));
        assert!(!world.damage(pilot, 1.0), "already gone");
    }

    #[test]
    fn records_impacts_with_obstacles() {
        let mut world = World::with_obstacles(vec![Obstacle {
            x: 4.0,
            z: 0.0,
            radius: 1.0,
        }]);
        let pilot = world.spawn_pilot_at((0.0, 0.0), 0.0);
        world.set_input(
            pilot,
            ShipInput {
                thrust: 1.0,
                turn: 0.0,
                cruise: false,
            },
        );
        let mut hit = false;
        for _ in 0..40 {
            world.step();
            hit |= world
                .impacts()
                .iter()
                .any(|(id, speed)| *id == pilot && *speed > 0.0);
        }
        assert!(hit);
    }

    #[test]
    fn spawns_an_npc_away_from_the_home_it_patrols() {
        let mut world = World::new();
        let drone = world.spawn_npc_at(ShipKind::Drone, (50.0, 0.0), (0.0, 0.0));
        let ship = world.ship(drone).unwrap();
        assert_eq!((ship.state.x, ship.state.z), (50.0, 0.0));
        assert_eq!(ship.home, (0.0, 0.0));
    }

    #[test]
    fn restores_a_pilot_under_their_old_id() {
        let mut world = World::new();
        let pilot = world.spawn_pilot_at((0.0, 0.0), 0.0);
        let state = world.ship(pilot).unwrap().state;
        world.remove_ship(pilot);
        world.restore_pilot(pilot, ShipState { x: 5.0, ..state }, 40.0);
        let ship = world.ship(pilot).unwrap();
        assert_eq!((ship.state.x, ship.hull), (5.0, 40.0));
        assert_ne!(
            world.spawn_npc(ShipKind::Drone, (1.0, 1.0)),
            pilot,
            "ids stay unique"
        );
        world.set_tick(Tick::new(500));
        assert_eq!(world.tick(), Tick::new(500));
    }
}
