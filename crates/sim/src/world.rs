//! The state of one expedition instance.

use crate::Tick;
use crate::collision::{self, Obstacle, SHIP_RADIUS};
use crate::ship::{self, STARTER_HANDLING, ShipInput, ShipState};

/// Identifies a ship within one instance. Small, because it crosses the wire.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ShipId(pub u16);

/// A ship and the input it is currently flying with.
#[derive(Debug, Clone, PartialEq)]
pub struct Ship {
    pub id: ShipId,
    pub state: ShipState,
    pub input: ShipInput,
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

    /// Adds a ship at the next spawn point and returns its id.
    pub fn spawn_ship(&mut self) -> ShipId {
        let id = ShipId(self.next_id);
        self.next_id = self.next_id.wrapping_add(1);
        let slot = f32::from(id.0 % SPAWN_SLOTS);
        let angle = slot * std::f32::consts::TAU / f32::from(SPAWN_SLOTS);
        self.ships.push(Ship {
            id,
            state: ShipState {
                x: angle.cos() * SPAWN_RADIUS,
                z: angle.sin() * SPAWN_RADIUS,
                heading: angle,
                speed: 0.0,
            },
            input: ShipInput::default(),
        });
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

    /// Advances every ship by one step, then resolves collisions: ships push
    /// one another apart, and obstacles last, so no ship ends a step inside one.
    pub fn step(&mut self) {
        for ship in &mut self.ships {
            ship.state = ship::step(ship.state, ship.input, &STARTER_HANDLING);
        }
        for i in 0..self.ships.len() {
            let (done, rest) = self.ships.split_at_mut(i + 1);
            let Some(a) = done.last_mut() else { continue };
            for b in rest {
                collision::separate(&mut a.state, SHIP_RADIUS, &mut b.state, SHIP_RADIUS);
            }
        }
        for ship in &mut self.ships {
            ship.state = collision::resolve_obstacles(ship.state, SHIP_RADIUS, &self.obstacles);
        }
        self.tick = self.tick.next();
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
            assert!((sa.x - sb.x).hypot(sa.z - sb.z) >= 2.0 * SHIP_RADIUS - 1e-3);
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
            assert!((state.x - rock.x).hypot(state.z) >= rock.radius + SHIP_RADIUS - 1e-3);
        }
        assert!(world.ship(id).unwrap().state.x < rock.x);
    }

    #[test]
    fn ids_are_not_reused_after_removal() {
        let mut world = World::new();
        let first = world.spawn_ship();
        world.remove_ship(first);
        assert_ne!(world.spawn_ship(), first);
    }
}
