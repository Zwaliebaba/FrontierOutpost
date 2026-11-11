//! Ship movement on the gameplay plane.
//!
//! A ship is steered directly: thrust along its heading and turn. Motion stays
//! on the plane (x, z); height is presentation only. The same step runs on the
//! server and, through `frontier-sim-wasm`, in the browser to predict the
//! player's own ship, so it must depend only on its arguments.

/// Seconds per simulation step.
pub const DT: f32 = 1.0 / crate::TICKS_PER_SECOND as f32;

/// Ships stay within this distance of the origin on each axis, in metres. It
/// is inside the ±1,024 m that a snapshot can carry.
pub const ARENA_HALF_WIDTH: f32 = 1_000.0;

/// What a pilot asks for this step. Both values are clamped to -1..=1.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct ShipInput {
    /// 1 is full thrust, -1 brakes. A ship never flies backwards.
    pub thrust: f32,
    /// 1 turns clockwise seen from above (from +x toward +z), -1 the other way.
    pub turn: f32,
    /// The cruise drive is engaged: [`CRUISE_FACTOR`] times the speed and
    /// acceleration. Only the run decides whether a ship may engage it.
    pub cruise: bool,
}

/// How much faster a ship flies on its cruise drive, a placeholder: the owner
/// found the three-minute legs of design section 10 too long to fly at normal
/// speed, and chose a drive over a smaller site.
pub const CRUISE_FACTOR: f32 = 3.0;
/// How quickly a ship sheds speed above its cap when the drive drops out,
/// metres per second squared: a second or so from cruise to normal speed.
pub const CRUISE_BRAKING: f32 = 12.0;

/// Where a ship is and how it moves.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct ShipState {
    pub x: f32,
    pub z: f32,
    /// Radians; 0 faces +x and the angle grows toward +z. Kept in 0..TAU.
    pub heading: f32,
    /// Metres per second along the heading, never negative.
    pub speed: f32,
}

impl ShipState {
    /// Where the ship is on the plane.
    #[must_use]
    pub const fn into_xz(self) -> (f32, f32) {
        (self.x, self.z)
    }

    /// Velocity on the plane, in metres per second.
    #[must_use]
    pub fn velocity(&self) -> (f32, f32) {
        let (sin, cos) = self.heading.sin_cos();
        (cos * self.speed, sin * self.speed)
    }
}

/// How a hull handles. Placeholders, tuned in play.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Handling {
    pub max_speed: f32,
    /// Metres per second gained per second at full thrust.
    pub acceleration: f32,
    /// Radians per second at full turn.
    pub turn_rate: f32,
    /// Fraction of speed lost per second.
    pub drag: f32,
}

/// The starter hull, the only one in Stage 1.
pub const STARTER_HANDLING: Handling = Handling {
    max_speed: 9.0,
    acceleration: 7.0,
    turn_rate: 2.4,
    drag: 0.5,
};

/// Advances a ship by one step of [`DT`].
#[must_use]
pub fn step(state: ShipState, input: ShipInput, handling: &Handling) -> ShipState {
    let thrust = clamp_unit(input.thrust);
    let turn = clamp_unit(input.turn);

    let heading =
        (state.heading + turn * handling.turn_rate * DT).rem_euclid(std::f32::consts::TAU);
    let factor = if input.cruise { CRUISE_FACTOR } else { 1.0 };
    let cap = handling.max_speed * factor;
    let next = state.speed + thrust * handling.acceleration * factor * DT
        - handling.drag * state.speed * DT;
    // Above the cap, as when the drive drops out, the ship slows at a fixed
    // rate rather than snapping to normal speed.
    let speed = if next > cap {
        (state.speed - CRUISE_BRAKING * DT).max(cap)
    } else {
        next
    }
    .max(0.0);
    let (sin, cos) = heading.sin_cos();
    ShipState {
        x: (state.x + cos * speed * DT).clamp(-ARENA_HALF_WIDTH, ARENA_HALF_WIDTH),
        z: (state.z + sin * speed * DT).clamp(-ARENA_HALF_WIDTH, ARENA_HALF_WIDTH),
        heading,
        speed,
    }
}

/// Clamps to -1..=1 and treats NaN as no input, so a malformed input cannot
/// corrupt the state.
fn clamp_unit(value: f32) -> f32 {
    if value.is_nan() {
        0.0
    } else {
        value.clamp(-1.0, 1.0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f32::consts::{FRAC_PI_2, TAU};

    const FULL: ShipInput = ShipInput {
        thrust: 1.0,
        turn: 0.0,
        cruise: false,
    };

    fn run(mut state: ShipState, input: ShipInput, steps: usize) -> ShipState {
        for _ in 0..steps {
            state = step(state, input, &STARTER_HANDLING);
        }
        state
    }

    #[test]
    fn full_thrust_moves_along_the_heading_up_to_top_speed() {
        let state = run(ShipState::default(), FULL, 200);
        assert!(state.x > 0.0);
        assert!(state.z.abs() < 1e-4);
        assert!(state.speed <= STARTER_HANDLING.max_speed);
        assert!(state.speed > STARTER_HANDLING.max_speed * 0.99);
    }

    #[test]
    fn a_ship_facing_plus_z_moves_along_plus_z() {
        let facing = ShipState {
            heading: FRAC_PI_2,
            ..ShipState::default()
        };
        let state = run(facing, FULL, 20);
        assert!(state.z > 0.0);
        assert!(state.x.abs() < 1e-4);
    }

    #[test]
    fn positive_turn_turns_clockwise_seen_from_above() {
        let input = ShipInput {
            thrust: 0.0,
            turn: 1.0,
            cruise: false,
        };
        let state = run(ShipState::default(), input, 1);
        assert!((state.heading - STARTER_HANDLING.turn_rate * DT).abs() < 1e-6);
    }

    #[test]
    fn heading_stays_within_one_turn() {
        let left = ShipInput {
            thrust: 0.0,
            turn: -1.0,
            cruise: false,
        };
        let state = run(ShipState::default(), left, 1);
        assert!((0.0..TAU).contains(&state.heading));
        let right = ShipInput {
            thrust: 0.0,
            turn: 1.0,
            cruise: false,
        };
        let state = run(ShipState::default(), right, 1_000);
        assert!((0.0..TAU).contains(&state.heading));
    }

    #[test]
    fn braking_stops_without_reversing() {
        let moving = ShipState {
            speed: 5.0,
            ..ShipState::default()
        };
        let brake = ShipInput {
            thrust: -1.0,
            turn: 0.0,
            cruise: false,
        };
        let state = run(moving, brake, 100);
        assert_eq!(state.speed, 0.0);
    }

    #[test]
    fn drag_slows_a_coasting_ship() {
        let moving = ShipState {
            speed: 8.0,
            ..ShipState::default()
        };
        let state = run(moving, ShipInput::default(), 20);
        assert!(state.speed < 8.0 && state.speed > 0.0);
    }

    #[test]
    fn out_of_range_and_nan_inputs_are_safe() {
        let wild = ShipInput {
            thrust: 50.0,
            turn: -50.0,
            cruise: false,
        };
        let full = ShipInput {
            thrust: 1.0,
            turn: -1.0,
            cruise: false,
        };
        assert_eq!(
            step(ShipState::default(), wild, &STARTER_HANDLING),
            step(ShipState::default(), full, &STARTER_HANDLING)
        );
        let nan = ShipInput {
            thrust: f32::NAN,
            turn: f32::NAN,
            cruise: false,
        };
        assert_eq!(
            step(ShipState::default(), nan, &STARTER_HANDLING),
            ShipState::default()
        );
    }

    #[test]
    fn ships_cannot_leave_the_arena() {
        let at_edge = ShipState {
            x: ARENA_HALF_WIDTH - 0.1,
            speed: 9.0,
            ..ShipState::default()
        };
        let state = run(at_edge, FULL, 100);
        assert_eq!(state.x, ARENA_HALF_WIDTH);
    }

    #[test]
    fn velocity_follows_heading_and_speed() {
        let state = ShipState {
            heading: FRAC_PI_2,
            speed: 4.0,
            ..ShipState::default()
        };
        let (vx, vz) = state.velocity();
        assert!(vx.abs() < 1e-5);
        assert!((vz - 4.0).abs() < 1e-5);
    }

    #[test]
    fn the_cruise_drive_raises_the_cap_and_sheds_speed_gradually_after() {
        let mut state = ShipState::default();
        let cruise = ShipInput {
            thrust: 1.0,
            turn: 0.0,
            cruise: true,
        };
        for _ in 0..200 {
            state = step(state, cruise, &STARTER_HANDLING);
        }
        let top = STARTER_HANDLING.max_speed * CRUISE_FACTOR;
        assert!((state.speed - top).abs() < 1e-3, "{}", state.speed);
        // The drive drops out: one step sheds CRUISE_BRAKING, not everything.
        let after = step(
            state,
            ShipInput {
                cruise: false,
                ..cruise
            },
            &STARTER_HANDLING,
        );
        assert!((after.speed - (top - CRUISE_BRAKING * DT)).abs() < 1e-3);
        let mut slowing = after;
        for _ in 0..100 {
            slowing = step(
                slowing,
                ShipInput {
                    cruise: false,
                    ..cruise
                },
                &STARTER_HANDLING,
            );
        }
        assert!((slowing.speed - STARTER_HANDLING.max_speed).abs() < 1e-3);
    }
}
