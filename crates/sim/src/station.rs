//! The station at a site's dock: its solid hull, hangar mouth and approach
//! lane (design section 11, plan 9.4).
//!
//! The rules own this geometry and the art pipeline checks the exterior
//! model against it (`Design/Art/tools/game_data.py`), so a re-export can
//! never change how ships dock. Every number is in metres at the scale the
//! exterior was authored at (about 2.2 times the old station); plan 9.1's
//! greybox may change it, and the model with it.
//!
//! Positions are given in the station's own frame, `(across, along)`, which
//! is the exterior's Blender X and Y: the hangar mouth lies on `+along`. The
//! station stands at the site's dock and is turned so its mouth faces the
//! site's centre, the way ships launch.

use crate::collision::{Obstacle, SHIP_RADIUS};
use crate::site::SiteTemplate;

/// From the station's centre to the hangar mouth, along its axis.
pub const MOUTH_DISTANCE: f32 = 14.0;
/// The mouth's clear opening across the lane.
pub const MOUTH_WIDTH: f32 = 8.0;
/// The mouth's clear height; for the art check only, ships fly on one plane.
pub const MOUTH_HEIGHT: f32 = 3.5;
/// The straight approach out of the mouth that nothing may block (the art
/// contract's `approach_length`). A launch hands over control at its end.
pub const LANE_LENGTH: f32 = 30.0;
/// No part of the hull reaches farther from the centre than this.
pub const FOOTPRINT_RADIUS: f32 = 16.4;
/// Where a departing ship starts inside the hangar, along the axis.
pub const HANGAR_START: f32 = 8.0;
/// The hangar's back wall, along the axis: the hub's drum stands behind it.
pub const HANGAR_BACK: f32 = 6.6;
/// The hangar walls' inner faces, either side of the axis.
pub const HANGAR_HALF_WIDTH: f32 = 4.2;
/// How far in front of the mouth a ship moving inward is captured and drawn
/// in: docking is still flown, but forgiving at 150 ms of latency.
pub const CAPTURE_DEPTH: f32 = 8.0;
/// The slowest inward speed that counts as flying in rather than drifting,
/// m/s.
pub const CAPTURE_SPEED: f32 = 0.25;

/// The hub's drum, centred behind the station's centre.
const HUB: (f32, f32, f32) = (0.0, -1.0, 7.6);
/// The inhabited ring, as a chain of circles along its middle.
const RING_MID: f32 = 14.4;
const RING_RADIUS: f32 = 1.9;
const RING_CIRCLES: u32 = 60;
/// The hangar's cheek walls, either side of the lane.
const CHEEK_ACROSS: f32 = 5.2;
const CHEEK_RADIUS: f32 = 1.0;

/// The station standing at a dock, facing a direction.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Station {
    centre: (f32, f32),
    forward: (f32, f32),
}

impl Station {
    /// The station at `template`'s dock, its mouth facing the site's centre.
    #[must_use]
    pub fn for_site(template: &SiteTemplate) -> Self {
        let (dx, dz) = template.dock;
        let (cx, cz) = template.centre;
        Self::new(template.dock, (cx - dx, cz - dz))
    }

    /// The station centred on `centre`, its mouth facing along `facing`.
    #[must_use]
    pub fn new(centre: (f32, f32), facing: (f32, f32)) -> Self {
        let length = facing.0.hypot(facing.1);
        let forward = if length > f32::EPSILON {
            (facing.0 / length, facing.1 / length)
        } else {
            (1.0, 0.0)
        };
        Self { centre, forward }
    }

    #[must_use]
    pub fn centre(&self) -> (f32, f32) {
        self.centre
    }

    /// The unit direction out of the mouth.
    #[must_use]
    pub fn forward(&self) -> (f32, f32) {
        self.forward
    }

    /// The heading out of the mouth, in the ships' convention.
    #[must_use]
    pub fn heading(&self) -> f32 {
        self.forward.1.atan2(self.forward.0)
    }

    /// The unit direction of `+across`. It is the forward direction turned
    /// so that the exterior, drawn with the same turn, is not mirrored.
    fn across(&self) -> (f32, f32) {
        (-self.forward.1, self.forward.0)
    }

    /// The world position of a point in the station's frame.
    #[must_use]
    pub fn world(&self, (across, along): (f32, f32)) -> (f32, f32) {
        let (ax, az) = self.across();
        let (fx, fz) = self.forward;
        (
            self.centre.0 + across * ax + along * fx,
            self.centre.1 + across * az + along * fz,
        )
    }

    /// A world position in the station's frame.
    #[must_use]
    pub fn local(&self, (x, z): (f32, f32)) -> (f32, f32) {
        let (dx, dz) = (x - self.centre.0, z - self.centre.1);
        let (ax, az) = self.across();
        let (fx, fz) = self.forward;
        (dx * ax + dz * az, dx * fx + dz * fz)
    }

    /// The centre of the hangar mouth.
    #[must_use]
    pub fn mouth(&self) -> (f32, f32) {
        self.world((0.0, MOUTH_DISTANCE))
    }

    /// The end of the approach lane, where a launch hands over control.
    #[must_use]
    pub fn lane_end(&self) -> (f32, f32) {
        self.world((0.0, MOUTH_DISTANCE + LANE_LENGTH))
    }

    /// Where a departing ship waits inside the hangar.
    #[must_use]
    pub fn hangar_start(&self) -> (f32, f32) {
        self.world((0.0, HANGAR_START))
    }

    /// How far along the lane, from the hangar start, `position` lies.
    #[must_use]
    pub fn along(&self, position: (f32, f32)) -> f32 {
        self.local(position).1
    }

    /// The solid hull: the hub, the ring and the hangar's walls, leaving the
    /// mouth and its lane open. Ships collide with these as with asteroids.
    #[must_use]
    pub fn obstacles(&self) -> Vec<Obstacle> {
        let mut circles = vec![HUB];
        for i in 0..RING_CIRCLES {
            let angle = std::f32::consts::TAU * i as f32 / RING_CIRCLES as f32;
            let (across, along) = (RING_MID * angle.cos(), RING_MID * angle.sin());
            // The ring is open where the hangar meets it.
            if along > 0.0 && across.abs() < HANGAR_HALF_WIDTH + RING_RADIUS {
                continue;
            }
            circles.push((across, along, RING_RADIUS));
        }
        let mut along = HANGAR_BACK - 1.6;
        while along <= MOUTH_DISTANCE - 0.5 {
            for side in [-1.0, 1.0] {
                circles.push((side * CHEEK_ACROSS, along, CHEEK_RADIUS));
            }
            along += 1.0;
        }
        circles
            .into_iter()
            .map(|(across, along, radius)| {
                let (x, z) = self.world((across, along));
                Obstacle { x, z, radius }
            })
            .collect()
    }

    /// Whether a ship at `position` moving at `velocity` is flying into the
    /// mouth: within the lane's width, from just inside the mouth to
    /// [`CAPTURE_DEPTH`] in front of it, and moving inward.
    #[must_use]
    pub fn captures(&self, position: (f32, f32), velocity: (f32, f32)) -> bool {
        let (across, along) = self.local(position);
        let inward = -(velocity.0 * self.forward.0 + velocity.1 * self.forward.1);
        across.abs() <= MOUTH_WIDTH / 2.0
            && (HANGAR_START..=MOUTH_DISTANCE + CAPTURE_DEPTH).contains(&along)
            && inward >= CAPTURE_SPEED
    }

    /// Whether `position` is inside the hangar, behind the mouth.
    #[must_use]
    pub fn in_hangar(&self, position: (f32, f32)) -> bool {
        let (across, along) = self.local(position);
        across.abs() <= HANGAR_HALF_WIDTH && (HANGAR_BACK..=MOUTH_DISTANCE).contains(&along)
    }

    /// Whether a ship at `position` would touch the station's footprint.
    #[must_use]
    pub fn overlaps_footprint(&self, position: (f32, f32)) -> bool {
        let (x, z) = position;
        (x - self.centre.0).hypot(z - self.centre.1) < FOOTPRINT_RADIUS + SHIP_RADIUS
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const EPSILON: f32 = 1e-4;

    fn close(a: (f32, f32), b: (f32, f32)) -> bool {
        (a.0 - b.0).abs() < EPSILON && (a.1 - b.1).abs() < EPSILON
    }

    fn stations() -> Vec<Station> {
        [(1.0, 0.0), (0.0, 1.0), (-1.0, 0.3), (0.4, -2.0)]
            .into_iter()
            .map(|facing| Station::new((-120.0, 7.0), facing))
            .collect()
    }

    fn overlaps(obstacles: &[Obstacle], (x, z): (f32, f32), radius: f32) -> bool {
        obstacles
            .iter()
            .any(|o| (x - o.x).hypot(z - o.z) < o.radius + radius)
    }

    #[test]
    fn faces_the_site_from_the_dock() {
        let template = crate::site::COMPACT_SITE;
        let station = Station::for_site(&template);
        let (dx, dz) = template.dock;
        let (cx, cz) = template.centre;
        let (mx, mz) = station.mouth();
        // The mouth lies between the dock and the site, on the line joining them.
        let to_site = (cx - dx, cz - dz);
        let to_mouth = (mx - dx, mz - dz);
        let cross = to_site.0 * to_mouth.1 - to_site.1 * to_mouth.0;
        assert!(cross.abs() < 1e-2, "{cross}");
        assert!(to_site.0 * to_mouth.0 + to_site.1 * to_mouth.1 > 0.0);
        assert!((to_mouth.0.hypot(to_mouth.1) - MOUTH_DISTANCE).abs() < EPSILON);
    }

    #[test]
    fn frames_round_trip_and_keep_the_exterior_unmirrored() {
        for station in stations() {
            for point in [(0.0, 0.0), (3.0, -2.0), (-7.5, 14.0)] {
                assert!(close(station.local(station.world(point)), point));
            }
            // As in the exterior, `+across` is to the right of `+along` seen
            // from above: forward turned a quarter towards +across is +x then +z.
            let (ax, az) = station.across();
            let (fx, fz) = station.forward();
            assert!((fx * az - fz * ax - 1.0).abs() < EPSILON);
        }
    }

    #[test]
    fn the_lane_is_open_from_the_hangar_to_its_end() {
        for station in stations() {
            let obstacles = station.obstacles();
            let mut along = HANGAR_START;
            while along <= MOUTH_DISTANCE + LANE_LENGTH {
                // A ship may stray from the axis by this much and still pass.
                for across in [-2.5, 0.0, 2.5] {
                    let point = station.world((across, along));
                    assert!(
                        !overlaps(&obstacles, point, SHIP_RADIUS),
                        "blocked at {across}, {along}"
                    );
                }
                along += 0.5;
            }
        }
    }

    #[test]
    fn the_hull_is_solid_everywhere_but_the_mouth() {
        let station = stations()[0];
        let obstacles = station.obstacles();
        // A ship on the ring's line touches the hull at every angle outside
        // the hangar's slot, so it can enter only through the mouth.
        for step in 0..360 {
            let angle = (step as f32).to_radians();
            let (across, along) = (RING_MID * angle.cos(), RING_MID * angle.sin());
            if along > 0.0 && across.abs() < HANGAR_HALF_WIDTH {
                continue;
            }
            let point = station.world((across, along));
            assert!(
                overlaps(&obstacles, point, SHIP_RADIUS),
                "gap at {step} degrees"
            );
        }
        // The hangar's walls close its sides between the ring and the hub.
        for along in [7.0, 9.0, 11.0, 13.0] {
            for side in [-1.0, 1.0] {
                let point = station.world((side * (HANGAR_HALF_WIDTH + 1.3), along));
                assert!(overlaps(&obstacles, point, SHIP_RADIUS), "{side} {along}");
            }
        }
    }

    #[test]
    fn every_obstacle_lies_within_the_footprint() {
        let station = stations()[1];
        for o in station.obstacles() {
            let reach = (o.x - station.centre.0).hypot(o.z - station.centre.1) + o.radius;
            assert!(reach <= FOOTPRINT_RADIUS + EPSILON, "{reach}");
        }
    }

    #[test]
    fn captures_a_ship_flying_into_the_mouth_only() {
        let station = stations()[2];
        let (fx, fz) = station.forward();
        let inward = (-fx * 2.0, -fz * 2.0);
        let outward = (fx * 2.0, fz * 2.0);
        let front = station.world((0.0, MOUTH_DISTANCE + 4.0));
        assert!(station.captures(front, inward));
        assert!(!station.captures(front, outward));
        assert!(!station.captures(front, (0.0, 0.0)));
        // Beside the lane, or too far out, it is not flying in.
        assert!(!station.captures(station.world((5.0, MOUTH_DISTANCE + 4.0)), inward));
        assert!(!station.captures(
            station.world((0.0, MOUTH_DISTANCE + CAPTURE_DEPTH + 1.0)),
            inward
        ));
        assert!(station.in_hangar(station.hangar_start()));
        assert!(!station.in_hangar(front));
    }
}
