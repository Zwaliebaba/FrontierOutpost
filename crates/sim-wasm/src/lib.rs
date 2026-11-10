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
}
