//! The jump drive: the owner's answer to the first playtest's "travel takes
//! too long", in place of the cruise drive alone. A pilot picks the dock or a
//! pocket, the drive spools for [`SPOOL_TICKS`], and the ship arrives near it
//! at rest.
//!
//! Its limits keep design section 2's risk on the way home: it spools only
//! while the cruise drive could engage (not while a hostile has the ship
//! locked or the extractor works), and a jump home with cargo lands short of
//! the dock, outside the site, so the return leg's interceptors still stand
//! between the ship and the berth. A pocket jump lands outside the pocket,
//! facing it, so the pilot sees what guards it before going in.
//!
//! The spool time and distances are placeholders, to be tuned in play.

use crate::TICKS_PER_SECOND;
use crate::ship::{ARENA_HALF_WIDTH, ShipState};
use crate::site::SiteTemplate;

/// How long the drive spools before the ship jumps.
pub const SPOOL_TICKS: u32 = 6 * TICKS_PER_SECOND;
/// How far outside a pocket's edge a jump to it arrives, metres.
pub const POCKET_STANDOFF: f32 = 40.0;
/// How far from the dock an empty ship jumping home arrives, metres: outside
/// the berth, so docking is still flown.
pub const DOCK_STANDOFF: f32 = 40.0;
/// How far beyond the interceptors' reach a laden ship jumping home arrives,
/// metres, so they appear between it and the dock.
pub const LADEN_MARGIN: f32 = 40.0;

/// Where a pilot asks to jump.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Target {
    Dock,
    /// A pocket, by its index in the site.
    Pocket(usize),
}

/// Why a jump did not start.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Refusal {
    /// The ship is docked, destroyed, held inert or the run is over.
    NotFlying,
    /// A hostile has the ship locked.
    Hunted,
    /// The extractor is working.
    Extracting,
    /// The pocket does not exist, or the crew may not enter it.
    Unavailable,
    /// The ship is already there.
    AlreadyThere,
}

/// Why a spooling jump stopped before the ship jumped.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Cancel {
    /// The pilot cancelled it.
    Pilot,
    /// A hostile locked the ship.
    Hunted,
    /// The extractor started.
    Extracting,
}

/// A jump spooling up.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Spool {
    pub target: Target,
    pub ticks_left: u32,
}

/// Where a ship jumping to a pocket centred at `centre` with `radius` arrives:
/// [`POCKET_STANDOFF`] outside its edge on the side the ship comes from,
/// facing its centre, at rest.
#[must_use]
pub fn pocket_arrival(
    from: ShipState,
    centre: (f32, f32),
    radius: f32,
    dock: (f32, f32),
) -> ShipState {
    // From the ship's side, or from the dock's when the ship is on the centre.
    let (dx, dz) = (centre.0 - from.x, centre.1 - from.z);
    let (dx, dz) = if dx.hypot(dz) > 1.0 {
        (dx, dz)
    } else {
        (centre.0 - dock.0, centre.1 - dock.1)
    };
    let length = dx.hypot(dz).max(f32::EPSILON);
    let (ux, uz) = (dx / length, dz / length);
    let back = radius + POCKET_STANDOFF;
    at_rest(centre.0 - ux * back, centre.1 - uz * back, ux, uz)
}

/// Where a ship jumping home arrives. Empty, it arrives [`DOCK_STANDOFF`]
/// from the dock. Laden, it arrives beyond the interceptors' reach but still
/// outside the site, so that leaving the site triggers the return leg and
/// its raiders appear between the ship and the dock.
#[must_use]
pub fn dock_arrival(from: ShipState, template: &SiteTemplate, laden: bool) -> ShipState {
    let dock = template.dock;
    let (dx, dz) = (from.x - dock.0, from.z - dock.1);
    let (dx, dz) = if dx.hypot(dz) > 1.0 {
        (dx, dz)
    } else {
        (template.centre.0 - dock.0, template.centre.1 - dock.1)
    };
    let length = dx.hypot(dz).max(f32::EPSILON);
    let (ux, uz) = (dx / length, dz / length);
    let distance = if laden {
        let to_site =
            (template.centre.0 - dock.0).hypot(template.centre.1 - dock.1) - template.radius - 10.0;
        (template.intercept_distance + LADEN_MARGIN)
            .min(to_site)
            .max(DOCK_STANDOFF)
    } else {
        DOCK_STANDOFF
    };
    at_rest(dock.0 + ux * distance, dock.1 + uz * distance, -ux, -uz)
}

/// A ship at rest at (x, z), facing along (ux, uz), inside the arena.
fn at_rest(x: f32, z: f32, ux: f32, uz: f32) -> ShipState {
    ShipState {
        x: x.clamp(-ARENA_HALF_WIDTH, ARENA_HALF_WIDTH),
        z: z.clamp(-ARENA_HALF_WIDTH, ARENA_HALF_WIDTH),
        heading: uz.atan2(ux).rem_euclid(std::f32::consts::TAU),
        speed: 0.0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::site::{COMPACT_SITE, FRONTIER_SITE};

    fn at(x: f32, z: f32) -> ShipState {
        ShipState {
            x,
            z,
            heading: 1.0,
            speed: 9.0,
        }
    }

    #[test]
    fn a_pocket_jump_arrives_outside_the_pocket_facing_it_at_rest() {
        let pocket = FRONTIER_SITE.pockets[0];
        let arrival = pocket_arrival(
            at(-900.0, 0.0),
            pocket.centre,
            pocket.radius,
            FRONTIER_SITE.dock,
        );
        let (cx, cz) = pocket.centre;
        let from_centre = (arrival.x - cx).hypot(arrival.z - cz);
        assert!((from_centre - (pocket.radius + POCKET_STANDOFF)).abs() < 0.01);
        assert!(arrival.x < cx, "on the side the ship came from");
        let facing = (arrival.heading.cos(), arrival.heading.sin());
        let to_centre = (
            (cx - arrival.x) / from_centre,
            (cz - arrival.z) / from_centre,
        );
        assert!(facing.0 * to_centre.0 + facing.1 * to_centre.1 > 0.999);
        assert_eq!(arrival.speed, 0.0);
    }

    #[test]
    fn a_ship_on_the_pocket_centre_arrives_on_the_dock_side() {
        let pocket = FRONTIER_SITE.pockets[0];
        let (cx, cz) = pocket.centre;
        let arrival = pocket_arrival(at(cx, cz), pocket.centre, pocket.radius, FRONTIER_SITE.dock);
        assert!(arrival.x < cx);
        assert!(arrival.x.is_finite() && arrival.z.is_finite());
    }

    #[test]
    fn an_empty_ship_jumping_home_arrives_just_outside_the_berth() {
        let arrival = dock_arrival(at(560.0, -60.0), &FRONTIER_SITE, false);
        let (dx, dz) = FRONTIER_SITE.dock;
        assert!(((arrival.x - dx).hypot(arrival.z - dz) - DOCK_STANDOFF).abs() < 0.01);
        assert!(
            arrival.heading.cos() < 0.0,
            "facing the dock, which is behind it"
        );
    }

    #[test]
    fn a_laden_ship_jumping_home_arrives_short_of_the_dock_and_outside_the_site() {
        for template in [FRONTIER_SITE, COMPACT_SITE] {
            let ore = template.pockets[0].centre;
            let arrival = dock_arrival(at(ore.0, ore.1), &template, true);
            let (dx, dz) = template.dock;
            let from_dock = (arrival.x - dx).hypot(arrival.z - dz);
            let (cx, cz) = template.centre;
            assert!(
                (arrival.x - cx).hypot(arrival.z - cz) > template.radius,
                "{}: outside the site, so the return leg starts",
                template.name
            );
            assert!(
                from_dock > DOCK_STANDOFF,
                "{}: short of the dock",
                template.name
            );
        }
        let arrival = dock_arrival(at(560.0, -60.0), &FRONTIER_SITE, true);
        let (dx, dz) = FRONTIER_SITE.dock;
        let from_dock = (arrival.x - dx).hypot(arrival.z - dz);
        assert!((from_dock - (FRONTIER_SITE.intercept_distance + LADEN_MARGIN)).abs() < 0.01);
    }
}
