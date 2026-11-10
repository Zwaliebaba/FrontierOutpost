//! The parts of `frontier-sim` the browser runs, exported to JavaScript.
//!
//! Build with `npm run build:sim` in `client/`, which compiles this crate for
//! `wasm32-unknown-unknown` and runs `wasm-bindgen` to write the JavaScript
//! bindings to `client/src/sim/pkg/`. Keep this layer thin: game rules belong
//! in `frontier-sim`, so the server and the browser share one implementation.

use wasm_bindgen::prelude::wasm_bindgen;

/// Simulation steps per second.
#[wasm_bindgen(js_name = ticksPerSecond)]
#[must_use]
pub fn ticks_per_second() -> u32 {
    frontier_sim::TICKS_PER_SECOND
}

/// Whole ticks that fit in `millis` milliseconds, rounded down.
#[wasm_bindgen(js_name = ticksForMillis)]
#[must_use]
pub fn ticks_for_millis(millis: u32) -> u32 {
    frontier_sim::ticks_for_millis(millis)
}

/// Advances the starter hull by one simulation step, exactly as the server
/// does, so the client can predict the player's own ship. Returns
/// `[x, z, heading, speed]`.
#[wasm_bindgen(js_name = stepShip)]
#[must_use]
pub fn step_ship(x: f32, z: f32, heading: f32, speed: f32, thrust: f32, turn: f32) -> Vec<f32> {
    use frontier_sim::ship::{STARTER_HANDLING, ShipInput, ShipState, step};
    let state = ShipState {
        x,
        z,
        heading,
        speed,
    };
    let next = step(state, ShipInput { thrust, turn }, &STARTER_HANDLING);
    vec![next.x, next.z, next.heading, next.speed]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn forwards_to_the_simulation() {
        assert_eq!(ticks_per_second(), frontier_sim::TICKS_PER_SECOND);
        assert_eq!(
            ticks_for_millis(1_500),
            frontier_sim::ticks_for_millis(1_500)
        );
    }

    #[test]
    fn steps_a_ship_as_the_server_does() {
        use frontier_sim::ship::{STARTER_HANDLING, ShipInput, ShipState, step};
        let state = ShipState {
            x: 1.0,
            z: -2.0,
            heading: 0.5,
            speed: 3.0,
        };
        let input = ShipInput {
            thrust: 1.0,
            turn: -0.5,
        };
        let expected = step(state, input, &STARTER_HANDLING);
        assert_eq!(
            step_ship(1.0, -2.0, 0.5, 3.0, 1.0, -0.5),
            vec![expected.x, expected.z, expected.heading, expected.speed]
        );
    }
}
