//! Weapons and what they leave behind: projectiles in flight and the events a
//! tick produces. The rules that use them are in [`crate::world::World::step`].

use crate::profile::ShipKind;
use crate::world::ShipId;

/// Projectiles in flight at once, per instance (plan, provisional cap). A
/// weapon holds its fire while the instance is at the cap.
pub const MAX_PROJECTILES: usize = 64;

/// A shot homing on its target. It hits unless the target is destroyed first.
#[derive(Debug, Clone, PartialEq)]
pub struct Projectile {
    pub shooter: ShipId,
    pub target: ShipId,
    pub x: f32,
    pub z: f32,
    /// Metres per second.
    pub speed: f32,
    pub damage: f32,
    /// Steps left before it gives up, so a shot can never fly for ever.
    pub ticks_left: u32,
}

/// Something that happened during a step, for the server to pass on.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Event {
    Fired {
        shooter: ShipId,
        target: ShipId,
        /// Metres per second, so a client can draw the shot's flight.
        speed: f32,
    },
    Destroyed {
        ship: ShipId,
        kind: ShipKind,
        /// The ship whose shot destroyed it.
        by: Option<ShipId>,
    },
}
