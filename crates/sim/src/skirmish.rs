//! A standing skirmish near the spawn, so combat can be built and tried before
//! sites exist. A placeholder, replaced by the site in plan M3.

use crate::profile::ShipKind;
use crate::world::{ShipId, World};

/// Twenty seconds at 20 ticks per second.
pub const RESPAWN_TICKS: u32 = 400;

/// The provisional cap on NPCs in one expedition (design section 10).
pub const MAX_NPCS: usize = 24;

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
    respawn_ticks: u32,
}

impl Skirmish {
    /// Spawns every NPC in the layout.
    pub fn populate(world: &mut World) -> Self {
        Self::populate_with(world, LAYOUT.len())
    }

    /// Spawns `npcs` NPCs, at most [`MAX_NPCS`]: the layout, then copies of it
    /// turned a quarter about the spawn each time. Fills an expedition to its
    /// cap for measuring it.
    pub fn populate_with(world: &mut World, npcs: usize) -> Self {
        let slots = LAYOUT
            .iter()
            .cycle()
            .take(npcs.min(MAX_NPCS))
            .enumerate()
            .map(|(index, &(kind, (x, z)))| {
                // At most four copies, so the quarter turn never repeats a position.
                let quarter = index / LAYOUT.len();
                let home = match quarter % 4 {
                    0 => (x, z),
                    1 => (-z, x),
                    2 => (-x, -z),
                    _ => (z, -x),
                };
                Slot {
                    kind,
                    home,
                    ship: Some(world.spawn_npc(kind, home)),
                    respawn_in: 0,
                }
            })
            .collect();
        Self {
            slots,
            respawn_ticks: RESPAWN_TICKS,
        }
    }

    /// Brings destroyed NPCs back after `ticks` instead of [`RESPAWN_TICKS`];
    /// one tick keeps the skirmish full.
    #[must_use]
    pub fn respawning_after(mut self, ticks: u32) -> Self {
        self.respawn_ticks = ticks.max(1);
        self
    }

    /// Call once per tick: counts down for destroyed NPCs and respawns them.
    pub fn step(&mut self, world: &mut World) {
        for slot in &mut self.slots {
            match slot.ship {
                Some(id) if world.ship(id).is_some() => {}
                Some(_) => {
                    slot.ship = None;
                    slot.respawn_in = self.respawn_ticks;
                }
                None if slot.respawn_in > 1 => slot.respawn_in -= 1,
                None => slot.ship = Some(world.spawn_npc(slot.kind, slot.home)),
            }
        }
    }

    /// NPCs the standard layout keeps alive at most.
    #[must_use]
    pub fn size() -> usize {
        LAYOUT.len()
    }

    /// NPCs this skirmish keeps alive at most.
    #[must_use]
    pub fn slots(&self) -> usize {
        self.slots.len()
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

    #[test]
    fn fills_to_the_cap_without_stacking_npcs() {
        let mut world = World::new();
        let skirmish = Skirmish::populate_with(&mut world, 100);
        assert_eq!(skirmish.slots(), MAX_NPCS);
        assert_eq!(world.ships().len(), MAX_NPCS);
        let homes: Vec<(f32, f32)> = world.ships().iter().map(|ship| ship.home).collect();
        for (i, a) in homes.iter().enumerate() {
            for b in &homes[i + 1..] {
                assert!((a.0 - b.0).hypot(a.1 - b.1) > 1.0, "{a:?} and {b:?}");
            }
        }
    }

    #[test]
    fn a_short_respawn_keeps_the_skirmish_full() {
        let mut world = World::new();
        let mut skirmish = Skirmish::populate_with(&mut world, 1).respawning_after(1);
        let first = world.ships()[0].id;
        world.remove_ship(first);
        skirmish.step(&mut world);
        assert!(world.ships().is_empty(), "noticed this tick");
        skirmish.step(&mut world);
        assert_eq!(world.ships().len(), 1, "back on the next");
    }
}
