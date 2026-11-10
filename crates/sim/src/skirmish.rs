//! A standing skirmish near the spawn, so combat can be built and tried before
//! sites exist. A placeholder, replaced by the site in plan M3.

use crate::profile::ShipKind;
use crate::world::{ShipId, World};

/// Twenty seconds at 20 ticks per second.
pub const RESPAWN_TICKS: u32 = 400;

const LAYOUT: [(ShipKind, (f32, f32)); 7] = [
    (ShipKind::Drone, (45.0, 10.0)),
    (ShipKind::Drone, (48.0, 16.0)),
    (ShipKind::Drone, (42.0, 4.0)),
    (ShipKind::Drone, (50.0, 6.0)),
    (ShipKind::Raider, (-40.0, -35.0)),
    (ShipKind::Raider, (-46.0, -30.0)),
    (ShipKind::Warden, (10.0, 70.0)),
];

#[derive(Debug, Clone)]
struct Slot {
    kind: ShipKind,
    home: (f32, f32),
    ship: Option<ShipId>,
    respawn_in: u32,
}

/// Keeps the layout's NPCs in the world, bringing each back some time after it
/// is destroyed.
#[derive(Debug, Clone)]
pub struct Skirmish {
    slots: Vec<Slot>,
}

impl Skirmish {
    /// Spawns every NPC in the layout.
    pub fn populate(world: &mut World) -> Self {
        let slots = LAYOUT
            .iter()
            .map(|&(kind, home)| Slot {
                kind,
                home,
                ship: Some(world.spawn_npc(kind, home)),
                respawn_in: 0,
            })
            .collect();
        Self { slots }
    }

    /// Call once per tick: counts down for destroyed NPCs and respawns them.
    pub fn step(&mut self, world: &mut World) {
        for slot in &mut self.slots {
            match slot.ship {
                Some(id) if world.ship(id).is_some() => {}
                Some(_) => {
                    slot.ship = None;
                    slot.respawn_in = RESPAWN_TICKS;
                }
                None if slot.respawn_in > 1 => slot.respawn_in -= 1,
                None => slot.ship = Some(world.spawn_npc(slot.kind, slot.home)),
            }
        }
    }

    /// NPCs the layout keeps alive at most.
    #[must_use]
    pub fn size() -> usize {
        LAYOUT.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn populates_every_kind_of_npc() {
        let mut world = World::new();
        Skirmish::populate(&mut world);
        let count = |kind| world.ships().iter().filter(|s| s.kind == kind).count();
        assert_eq!(count(ShipKind::Drone), 4);
        assert_eq!(count(ShipKind::Raider), 2);
        assert_eq!(count(ShipKind::Warden), 1);
    }

    #[test]
    fn brings_a_destroyed_npc_back_after_the_delay() {
        let mut world = World::new();
        let mut skirmish = Skirmish::populate(&mut world);
        let first = world.ships()[0].id;
        world.remove_ship(first);
        for _ in 0..RESPAWN_TICKS {
            skirmish.step(&mut world);
        }
        assert_eq!(world.ships().len(), Skirmish::size() - 1);
        skirmish.step(&mut world);
        assert_eq!(world.ships().len(), Skirmish::size());
        assert!(world.ship(first).is_none(), "it returns as a new ship");
    }
}
