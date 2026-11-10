//! What NPCs decide each tick: whom to target and how to steer (design §10).

use crate::profile::ShipKind;
use crate::ship::ShipInput;
use crate::world::{Ship, ShipId};

/// How far a warden may stray from its post, metres.
pub const WARDEN_LEASH: f32 = 12.0;
/// Radius of the loop drones and raiders fly around their home when idle.
const PATROL_RADIUS: f32 = 10.0;

/// The pilot an NPC should engage, if any.
///
/// - A drone takes the nearest extracting pilot it is aware of, else the
///   nearest pilot (design §10: drones swarm extracting ships).
/// - A raider takes the pilot carrying the most cargo, the nearest on a tie.
/// - A warden takes the nearest pilot it is aware of, measured from its post.
#[must_use]
pub fn choose_target(npc: &Ship, ships: &[Ship]) -> Option<ShipId> {
    let profile = npc.kind.profile();
    let from = match npc.kind {
        ShipKind::Warden => npc.home,
        _ => (npc.state.x, npc.state.z),
    };
    let distance = |ship: &Ship| (ship.state.x - from.0).hypot(ship.state.z - from.1);
    let candidates = ships
        .iter()
        .filter(|ship| ship.kind == ShipKind::Pilot && distance(ship) <= profile.awareness);
    match npc.kind {
        ShipKind::Raider => candidates
            .max_by(|a, b| {
                a.cargo
                    .cmp(&b.cargo)
                    .then_with(|| distance(b).total_cmp(&distance(a)))
            })
            .map(|ship| ship.id),
        ShipKind::Drone => candidates
            .min_by(|a, b| {
                b.extracting
                    .cmp(&a.extracting)
                    .then_with(|| distance(a).total_cmp(&distance(b)))
            })
            .map(|ship| ship.id),
        ShipKind::Warden => candidates
            .min_by(|a, b| distance(a).total_cmp(&distance(b)))
            .map(|ship| ship.id),
        ShipKind::Pilot => None,
    }
}

/// How an NPC steers toward its target, or around its home when it has none.
#[must_use]
pub fn steer(npc: &Ship, target: Option<&Ship>, tick: u64) -> ShipInput {
    let range = npc.kind.profile().weapon.range;
    match (npc.kind, target) {
        (ShipKind::Warden, target) => {
            let (hx, hz) = npc.home;
            let from_home = (npc.state.x - hx).hypot(npc.state.z - hz);
            match target {
                // Within its leash it closes on the target; beyond it, it goes back.
                Some(target) if from_home < WARDEN_LEASH => {
                    seek(npc, target.state.x, target.state.z, range * 0.8)
                }
                _ => seek(npc, hx, hz, 1.0),
            }
        }
        (_, Some(target)) => seek(npc, target.state.x, target.state.z, range * 0.7),
        (_, None) => {
            // A slow loop around home; each NPC starts at a different point on it.
            let angle = tick as f32 * 0.02 + f32::from(npc.id.0);
            let (hx, hz) = npc.home;
            seek(
                npc,
                hx + angle.cos() * PATROL_RADIUS,
                hz + angle.sin() * PATROL_RADIUS,
                0.5,
            )
        }
    }
}

/// Turns toward a point and closes on it, braking once within `stop_at` metres.
fn seek(npc: &Ship, x: f32, z: f32, stop_at: f32) -> ShipInput {
    let (dx, dz) = (x - npc.state.x, z - npc.state.z);
    let bearing = dz.atan2(dx);
    let mut off = (bearing - npc.state.heading).rem_euclid(std::f32::consts::TAU);
    if off > std::f32::consts::PI {
        off -= std::f32::consts::TAU;
    }
    let distance = dx.hypot(dz);
    let thrust = if distance <= stop_at {
        -1.0
    } else if off.abs() < 1.0 {
        1.0
    } else {
        0.3
    };
    ShipInput {
        thrust,
        turn: (off * 2.0).clamp(-1.0, 1.0),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ship::ShipState;

    fn ship(id: u16, kind: ShipKind, x: f32, z: f32) -> Ship {
        Ship::new(
            ShipId(id),
            kind,
            ShipState {
                x,
                z,
                heading: 0.0,
                speed: 0.0,
            },
        )
    }

    #[test]
    fn a_drone_targets_the_nearest_pilot_it_can_see() {
        let drone = ship(1, ShipKind::Drone, 0.0, 0.0);
        let ships = [
            ship(2, ShipKind::Pilot, 30.0, 0.0),
            ship(3, ShipKind::Pilot, 10.0, 0.0),
            ship(4, ShipKind::Raider, 1.0, 0.0),
            ship(5, ShipKind::Pilot, 500.0, 0.0),
        ];
        assert_eq!(choose_target(&drone, &ships), Some(ShipId(3)));
        assert_eq!(choose_target(&drone, &ships[3..]), None, "out of sight");
    }

    #[test]
    fn a_drone_prefers_an_extracting_pilot_to_a_nearer_one() {
        let drone = ship(1, ShipKind::Drone, 0.0, 0.0);
        let near = ship(2, ShipKind::Pilot, 5.0, 0.0);
        let mut working = ship(3, ShipKind::Pilot, 40.0, 0.0);
        working.extracting = true;
        assert_eq!(
            choose_target(&drone, &[near.clone(), working]),
            Some(ShipId(3))
        );
        assert_eq!(choose_target(&drone, &[near]), Some(ShipId(2)));
    }

    #[test]
    fn a_raider_targets_the_most_cargo_then_the_nearest() {
        let raider = ship(1, ShipKind::Raider, 0.0, 0.0);
        let mut laden = ship(2, ShipKind::Pilot, 50.0, 0.0);
        laden.cargo = 12;
        let near = ship(3, ShipKind::Pilot, 5.0, 0.0);
        assert_eq!(
            choose_target(&raider, &[near.clone(), laden]),
            Some(ShipId(2))
        );
        let far = ship(4, ShipKind::Pilot, 40.0, 0.0);
        assert_eq!(choose_target(&raider, &[far, near]), Some(ShipId(3)));
    }

    #[test]
    fn a_warden_judges_distance_from_its_post_not_from_itself() {
        let mut warden = ship(1, ShipKind::Warden, 10.0, 0.0);
        warden.home = (0.0, 0.0);
        let reach = ShipKind::Warden.profile().awareness;
        let beyond = ship(2, ShipKind::Pilot, -(reach + 1.0), 0.0);
        let within = ship(3, ShipKind::Pilot, -(reach - 1.0), 0.0);
        assert_eq!(choose_target(&warden, std::slice::from_ref(&beyond)), None);
        assert_eq!(choose_target(&warden, &[beyond, within]), Some(ShipId(3)));
    }

    #[test]
    fn npcs_never_target_each_other() {
        let drone = ship(1, ShipKind::Drone, 0.0, 0.0);
        let others = [
            ship(2, ShipKind::Raider, 2.0, 0.0),
            ship(3, ShipKind::Warden, 3.0, 0.0),
        ];
        assert_eq!(choose_target(&drone, &others), None);
    }

    #[test]
    fn a_warden_beyond_its_leash_heads_home_even_with_a_target() {
        let mut warden = ship(1, ShipKind::Warden, WARDEN_LEASH + 5.0, 0.0);
        warden.home = (0.0, 0.0);
        warden.state.heading = std::f32::consts::PI; // facing home
        let target = ship(2, ShipKind::Pilot, 60.0, 0.0); // behind it
        let input = steer(&warden, Some(&target), 0);
        assert_eq!(input.thrust, 1.0);
        assert!(input.turn.abs() < 1e-5, "it keeps facing home");
    }

    #[test]
    fn an_npc_closes_on_its_target_and_brakes_inside_its_range() {
        let drone = ship(1, ShipKind::Drone, 0.0, 0.0);
        let far = ship(2, ShipKind::Pilot, 40.0, 0.0);
        assert_eq!(steer(&drone, Some(&far), 0).thrust, 1.0);
        let close = ship(3, ShipKind::Pilot, 2.0, 0.0);
        assert_eq!(steer(&drone, Some(&close), 0).thrust, -1.0);
    }
}
