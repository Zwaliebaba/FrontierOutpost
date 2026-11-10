//! The expedition at its provisional caps (design section 10, plan work item
//! 2.6): four pilots, 24 NPCs and enough fire to keep 64 projectiles in
//! flight. The snapshot-size test and the `caps` benchmark both run it.

use frontier_protocol::snapshot::SnapshotError;
use frontier_sim::collision::asteroid_field;
use frontier_sim::profile::ShipKind;
use frontier_sim::skirmish::{MAX_NPCS, Skirmish};
use frontier_sim::world::{ShipId, World};

use crate::expedition::{ASTEROID_SEED, MAX_PILOTS, snapshot};

/// A full expedition in which every pilot keeps firing at the nearest hostile.
/// Destroyed pilots and NPCs are replaced at once, so it stays full.
pub struct Caps {
    world: World,
    skirmish: Skirmish,
    pilots: Vec<ShipId>,
}

impl Default for Caps {
    fn default() -> Self {
        Self::new()
    }
}

impl Caps {
    #[must_use]
    pub fn new() -> Self {
        let mut world = World::with_obstacles(asteroid_field(ASTEROID_SEED));
        // Destroyed NPCs return on the next tick, so the expedition stays full.
        let skirmish = Skirmish::populate_with(&mut world, MAX_NPCS).respawning_after(1);
        Self {
            world,
            skirmish,
            pilots: Vec::new(),
        }
    }

    #[must_use]
    pub fn world(&self) -> &World {
        &self.world
    }

    /// Advances one tick the way the expedition does and returns the snapshot
    /// datagram every pilot would be sent.
    ///
    /// # Errors
    /// Fails if the snapshot does not encode, which a full expedition must not do.
    pub fn step(&mut self) -> Result<Vec<u8>, SnapshotError> {
        let world = &mut self.world;
        self.pilots.retain(|&pilot| world.ship(pilot).is_some());
        while self.pilots.len() < MAX_PILOTS {
            self.pilots.push(world.spawn_ship());
        }
        for &pilot in &self.pilots {
            let locked = world
                .ship(pilot)
                .and_then(|ship| ship.lock)
                .is_some_and(|target| world.ship(target).is_some());
            if !locked {
                let target = nearest_hostile(world, pilot);
                world.set_lock(pilot, target);
            }
            world.set_firing(pilot, true);
        }
        self.skirmish.step(world);
        world.step();
        snapshot(world, 0).encode()
    }
}

fn nearest_hostile(world: &World, pilot: ShipId) -> Option<ShipId> {
    let from = world.ship(pilot)?.state;
    world
        .ships()
        .iter()
        .filter(|ship| ship.kind != ShipKind::Pilot)
        .min_by(|a, b| {
            let distance = |ship: &frontier_sim::world::Ship| {
                (ship.state.x - from.x).hypot(ship.state.z - from.z)
            };
            distance(a).total_cmp(&distance(b))
        })
        .map(|ship| ship.id)
}
