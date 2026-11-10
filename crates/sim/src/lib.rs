//! Frontier Outpost simulation.
//!
//! This crate holds the authoritative game rules. It does no I/O and does not
//! read the clock: it advances in fixed ticks and is handed its inputs. The
//! server runs it, and the browser runs its movement rules through
//! `frontier-sim-wasm` to predict the player's own ship.

pub mod collision;
pub mod combat;
pub mod npc;
pub mod outcome;
pub mod profile;
pub mod rng;
pub mod ship;
pub mod skirmish;
pub mod world;

/// Simulation steps per second (design section 10).
pub const TICKS_PER_SECOND: u32 = 20;

/// Whole ticks that fit in `millis` milliseconds, rounded down.
#[must_use]
pub const fn ticks_for_millis(millis: u32) -> u32 {
    // Widen first so the product cannot overflow.
    ((millis as u64 * TICKS_PER_SECOND as u64) / 1_000) as u32
}

/// A fixed simulation step. The server advances the world one tick at a time.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Tick(u64);

impl Tick {
    /// The first tick of a session.
    pub const ZERO: Self = Self(0);

    /// Creates a tick from its raw step count.
    #[must_use]
    pub const fn new(step: u64) -> Self {
        Self(step)
    }

    /// The raw step count.
    #[must_use]
    pub const fn get(self) -> u64 {
        self.0
    }

    /// The tick after this one.
    #[must_use]
    pub const fn next(self) -> Self {
        Self(self.0 + 1)
    }
}

#[cfg(test)]
mod tests {
    use super::Tick;

    #[test]
    fn counts_whole_ticks_in_a_duration() {
        use super::ticks_for_millis;
        assert_eq!(ticks_for_millis(0), 0);
        assert_eq!(ticks_for_millis(49), 0);
        assert_eq!(ticks_for_millis(50), 1);
        assert_eq!(ticks_for_millis(1_000), 20);
        assert_eq!(ticks_for_millis(u32::MAX), 85_899_345);
    }

    #[test]
    fn ticks_advance_in_order() {
        let first = Tick::ZERO;
        let second = first.next();
        assert!(second > first);
        assert_eq!(second.get(), 1);
    }
}
