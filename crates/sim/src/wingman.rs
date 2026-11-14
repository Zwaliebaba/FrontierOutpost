//! Owner-directed ally behaviour. It receives visible targets and explicit marks only;
//! seed values, relay locations and pocket selection never enter this module.

use serde::{Deserialize, Serialize};

use crate::ship::ShipInput;
use crate::world::Ship;

pub const FOLLOW_DISTANCE: f32 = 6.0;
pub const WEAPON_FACTOR: f32 = 0.65;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
pub enum Order {
    #[default]
    Follow,
    Hold {
        x: i32,
        z: i32,
    },
    Return,
    Extract {
        deposit: u32,
    },
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct Wingman {
    pub owner: u16,
    pub tier: u32,
    pub order: Order,
    #[serde(skip)]
    pub follow_jump: Option<crate::jump::Target>,
    /// A visible shot, not an NPC's private lock or awareness state.
    #[serde(skip)]
    pub attacker: Option<(u16, u32)>,
}

/// Divert around the nearest visible obstacle crossing the marked route.
#[must_use]
pub fn waypoint(
    ship: &Ship,
    goal: (f32, f32),
    obstacles: &[crate::collision::Obstacle],
) -> (f32, f32) {
    let (dx, dz) = (goal.0 - ship.state.x, goal.1 - ship.state.z);
    let distance = dx.hypot(dz);
    if distance < 1.0 {
        return goal;
    }
    let (ux, uz) = (dx / distance, dz / distance);
    let obstacle = obstacles
        .iter()
        .filter_map(|rock| {
            let (rx, rz) = (rock.x - ship.state.x, rock.z - ship.state.z);
            let along = rx * ux + rz * uz;
            let across = rx * -uz + rz * ux;
            (along > -rock.radius && along < distance.min(25.0) && across.abs() < rock.radius + 2.5)
                .then_some((rock, along, across))
        })
        .min_by(|a, b| a.1.total_cmp(&b.1));
    if let Some((rock, _, across)) = obstacle {
        let side = if across > 0.0 { -1.0 } else { 1.0 };
        let radius = rock.radius + 4.0;
        (rock.x - uz * radius * side, rock.z + ux * radius * side)
    } else {
        goal
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum Refusal {
    #[error("crew quarters must be powered for this order")]
    NeedsQuarters,
    #[error("this pilot has no wingman flying")]
    NotFlying,
    #[error("pilots and wingmen may not exceed four ships")]
    ShipLimit,
    #[error("choose a visible deposit after scanning the site")]
    Unrevealed,
    #[error("that mark is outside the flight area")]
    OutOfBounds,
}

/// Brake early enough to settle at a mark; never select a destination here.
#[must_use]
pub fn steer(ship: &Ship, point: (f32, f32), stop: f32, cruise: bool) -> ShipInput {
    let dx = point.0 - ship.state.x;
    let dz = point.1 - ship.state.z;
    let distance = dx.hypot(dz);
    let mut off = (dz.atan2(dx) - ship.state.heading).rem_euclid(std::f32::consts::TAU);
    if off > std::f32::consts::PI {
        off -= std::f32::consts::TAU;
    }
    let braking = ship.state.speed.max(0.0).powi(2) / 12.0;
    ShipInput {
        thrust: if distance <= stop + braking {
            -1.0
        } else if off.abs() < 0.7 {
            1.0
        } else {
            0.0
        },
        turn: if distance < stop {
            0.0
        } else {
            (off * 1.4).clamp(-1.0, 1.0)
        },
        cruise: cruise && distance > 45.0 && off.abs() < 0.3,
    }
}
