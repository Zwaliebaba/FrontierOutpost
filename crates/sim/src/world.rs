//! The state of one expedition instance.

use crate::Tick;
use crate::collision::{self, Obstacle};
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
    /// Cargo units carried. Always 0 until extraction (plan M3).
    pub cargo: u32,
    /// Where an NPC patrols, or a warden's post. A pilot's spawn point.
    pub home: (f32, f32),
    /// The ship this one has locked: chosen by its pilot, or by its NPC logic.
    pub lock: Option<ShipId>,
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
        }
    }
}

/// Every entity in an instance, advanced one tick at a time.
#[derive(Debug, Clone, Default)]
pub struct World {
    tick: Tick,
    ships: Vec<Ship>,
    obstacles: Vec<Obstacle>,
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

    /// Removes a ship. Returns whether it existed.
    pub fn remove_ship(&mut self, id: ShipId) -> bool {
        let before = self.ships.len();
        self.ships.retain(|ship| ship.id != id);
        self.ships.len() != before
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
        self.think();
        for ship in &mut self.ships {
            let handling = ship.kind.profile().handling;
            ship.state = ship::step(ship.state, ship.input, &handling);
        }
        for i in 0..self.ships.len() {
            let (done, rest) = self.ships.split_at_mut(i + 1);
            let Some(a) = done.last_mut() else { continue };
            for b in rest {
                let (ra, rb) = (a.kind.profile().radius, b.kind.profile().radius);
                collision::separate(&mut a.state, ra, &mut b.state, rb);
            }
        }
        for ship in &mut self.ships {
            let radius = ship.kind.profile().radius;
            ship.state = collision::resolve_obstacles(ship.state, radius, &self.obstacles);
        }
        self.tick = self.tick.next();
    }

    /// Lets every NPC choose a target and how to steer.
    fn think(&mut self) {
        let tick = self.tick.get();
        let decisions: Vec<(usize, Option<ShipId>, ShipInput)> = self
            .ships
            .iter()
            .enumerate()
            .filter(|(_, ship)| ship.kind != ShipKind::Pilot)
            .map(|(index, npc)| {
                let target = npc::choose_target(npc, &self.ships);
                let target_ship = target.and_then(|id| self.ship(id));
                (index, target, npc::steer(npc, target_ship, tick))
            })
            .collect();
        for (index, target, input) in decisions {
            if let Some(npc) = self.ships.get_mut(index) {
                npc.lock = target;
                npc.input = input;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
                turn: 0.0
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
    fn ids_are_not_reused_after_removal() {
        let mut world = World::new();
        let first = world.spawn_ship();
        world.remove_ship(first);
        assert_ne!(world.spawn_ship(), first);
    }
}
