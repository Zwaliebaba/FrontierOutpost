//! What the Stage 1 gates measure (design section 9, plan 7.1), recorded as
//! it happens: sessions, launches with the layout and fitting, the choices made
//! in each run, reconfigurations, and how each run ended. Events are append-
//! only rows in the world's database beside the ledger; `frontier-server
//! report` computes the gates from them.

use serde::{Deserialize, Serialize};

/// One installed facility, as a launch records the layout.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct Placed {
    pub socket: String,
    pub facility: String,
    /// The tier it runs at; 0 unpowered.
    pub run: u32,
}

/// Something that matters to a gate.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum GateEvent {
    /// A pilot signed in on a page.
    SessionStarted,
    /// That page's session ended, this long after it started.
    SessionEnded { millis: u64 },
    /// A pilot launched, or followed their host into a run.
    Launched {
        seed: u32,
        tier: u32,
        /// The crew the run is scaled for; 1 solo.
        crew: u32,
        /// "solo", "host" or "member".
        role: String,
        layout: Vec<Placed>,
        /// Every facility tier owned, as "facility:tier", sorted.
        unlocks: Vec<String>,
        /// The rig and modules flown, sorted.
        fitted: Vec<String>,
        /// Replacement price of what flies.
        stake: u32,
        supplies: u32,
    },
    /// A pilot entered a pocket.
    PocketEntered { pocket: String },
    /// A pilot started a scan.
    ScanStarted,
    /// A pilot started their extractor.
    ExtractorStarted { material: String },
    /// A pilot locked a hostile, or tried to beyond lock range.
    Locked {
        target: String,
        distance: f32,
        taken: bool,
    },
    /// A pilot's run ended.
    RunEnded {
        /// "docked", "destroyed" or "abandoned".
        ending: String,
        banked: u32,
        cargo_lost: u32,
        /// The rigs and modules destroyed with the ship, by identifier.
        lost: Vec<String>,
        approach_seconds: u32,
        objective_seconds: u32,
        return_seconds: u32,
    },
    /// The crew's pot was paid to this pilot.
    PotShare { share: u32, crew: u32 },
    /// The outpost changed: "bought", "reinstalled", "upgraded", "removed"
    /// or "powered".
    OutpostChanged {
        action: String,
        socket: String,
        facility: String,
        run: u32,
    },
}

impl GateEvent {
    /// The event's kind, as the database stores it.
    #[must_use]
    pub const fn kind(&self) -> &'static str {
        match self {
            Self::SessionStarted => "session_started",
            Self::SessionEnded { .. } => "session_ended",
            Self::Launched { .. } => "launched",
            Self::PocketEntered { .. } => "pocket_entered",
            Self::ScanStarted => "scan_started",
            Self::ExtractorStarted { .. } => "extractor_started",
            Self::Locked { .. } => "locked",
            Self::RunEnded { .. } => "run_ended",
            Self::PotShare { .. } => "pot_share",
            Self::OutpostChanged { .. } => "outpost_changed",
        }
    }

    /// Whether this is a choice made in a run, for the time to first decision
    /// (design section 9, gate 4).
    #[must_use]
    pub const fn is_decision(&self) -> bool {
        matches!(
            self,
            Self::PocketEntered { .. }
                | Self::ScanStarted
                | Self::ExtractorStarted { .. }
                | Self::Locked { .. }
        )
    }
}

/// Now, in Unix milliseconds, for recording.
#[must_use]
pub fn now_ms() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |since| {
            i64::try_from(since.as_millis()).unwrap_or(i64::MAX)
        })
}

/// Now, in Unix seconds: the clock the processor's batches are timed by.
#[must_use]
pub fn now_secs() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |since| since.as_secs())
}

/// An event as recorded: when, by whom, in which run.
#[derive(Debug, Clone, PartialEq)]
pub struct Recorded {
    /// Unix milliseconds.
    pub at: i64,
    pub account: i64,
    pub run: Option<String>,
    pub event: GateEvent,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn events_round_trip_as_tagged_json() {
        let event = GateEvent::Locked {
            target: "drone".into(),
            distance: 31.5,
            taken: true,
        };
        let json = serde_json::to_string(&event).unwrap();
        assert_eq!(
            json,
            r#"{"kind":"locked","target":"drone","distance":31.5,"taken":true}"#
        );
        assert_eq!(serde_json::from_str::<GateEvent>(&json).unwrap(), event);
        assert_eq!(event.kind(), "locked");
        assert!(event.is_decision());
        assert!(!GateEvent::SessionStarted.is_decision());
    }
}
