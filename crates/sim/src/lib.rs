//! Deterministic Frontier Outpost simulation.
//!
//! This crate holds the authoritative game rules. It must stay free of I/O,
//! wall-clock time, threads and unseeded randomness so the server can replay
//! and validate any session from its inputs.

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
    fn ticks_advance_in_order() {
        let first = Tick::ZERO;
        let second = first.next();
        assert!(second > first);
        assert_eq!(second.get(), 1);
    }
}
