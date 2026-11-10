//! The parts of `frontier-sim` the browser runs, exported to JavaScript.
//!
//! Build with `npm run build:sim` in `client/`, which compiles this crate for
//! `wasm32-unknown-unknown` and runs `wasm-bindgen` to write the JavaScript
//! bindings to `client/src/sim/pkg/`. Keep this layer thin: game rules belong
//! in `frontier-sim`, so the server and the browser share one implementation.

use std::cell::RefCell;

use frontier_sim::collision::{Obstacle, SHIP_RADIUS, resolve_obstacles};
use wasm_bindgen::prelude::wasm_bindgen;

thread_local! {
    /// The expedition's fixed obstacles, set once on joining.
    static OBSTACLES: RefCell<Vec<Obstacle>> = const { RefCell::new(Vec::new()) };
}

/// Sets the fixed obstacles that predicted ships collide with, as flat
/// `[x, z, radius, ...]` triples. A trailing partial triple is ignored.
#[wasm_bindgen(js_name = setObstacles)]
pub fn set_obstacles(triples: Vec<f32>) {
    let obstacles = triples
        .as_chunks::<3>()
        .0
        .iter()
        .map(|&[x, z, radius]| Obstacle { x, z, radius })
        .collect();
    OBSTACLES.with(|cell| *cell.borrow_mut() = obstacles);
}

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
/// does for a ship clear of other ships, so the client can predict the
/// player's own ship. Includes collisions with the obstacles from
/// [`set_obstacles`]. `cruise` engages the cruise drive, which the server
/// allows only outside the site and while no hostile has the ship locked.
/// Returns `[x, z, heading, speed]`.
#[wasm_bindgen(js_name = stepShip)]
#[must_use]
pub fn step_ship(
    x: f32,
    z: f32,
    heading: f32,
    speed: f32,
    thrust: f32,
    turn: f32,
    cruise: bool,
) -> Vec<f32> {
    use frontier_sim::ship::{STARTER_HANDLING, ShipInput, ShipState, step};
    let state = ShipState {
        x,
        z,
        heading,
        speed,
    };
    let moved = step(
        state,
        ShipInput {
            thrust,
            turn,
            cruise,
        },
        &STARTER_HANDLING,
    );
    let next = OBSTACLES.with(|cell| resolve_obstacles(moved, SHIP_RADIUS, &cell.borrow()));
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
    fn predicted_ships_collide_with_the_obstacles_set() {
        set_obstacles(vec![2.0, 0.0, 1.0, 99.0]);
        let next = step_ship(1.0, 0.0, 0.0, 0.0, 0.0, 0.0, false);
        assert!(next[0] <= 2.0 - 1.0 - SHIP_RADIUS + 1e-5, "{next:?}");
        set_obstacles(vec![]);
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
            cruise: false,
        };
        let expected = step(state, input, &STARTER_HANDLING);
        assert_eq!(
            step_ship(1.0, -2.0, 0.5, 3.0, 1.0, -0.5, false),
            vec![expected.x, expected.z, expected.heading, expected.speed]
        );
    }
}
