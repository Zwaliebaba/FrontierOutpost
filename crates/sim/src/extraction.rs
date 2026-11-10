//! A ship's extractor (design sections 3 and 10): switched on at a deposit, it
//! yields while the ship holds still beside it and builds heat as it goes. At
//! full heat it cuts out for 20 seconds and stays off, so nothing yields
//! without the pilot's attention. Venting pauses it for 5 seconds and clears
//! the heat.
//!
//! The design gives the cut-out and vent times; how fast heat builds, how close
//! and how still a ship must be are placeholders chosen here.

use crate::TICKS_PER_SECOND;

/// How close to a deposit's centre a ship must be to work it, metres.
pub const EXTRACT_RANGE: f32 = 6.0;
/// The fastest a ship may move and keep extracting, metres per second.
pub const MAX_EXTRACT_SPEED: f32 = 1.5;
/// Cargo the basic fitting carries (design section 10).
pub const BASIC_CAPACITY: u32 = 20;
/// Ticks of extraction from cold to full heat, before hazards and pockets.
/// A minute, so a vent a minute costs about 8% of the rate: design section 10
/// budgets 13 ore from 6.5 minutes at the basic 2 per minute.
pub const HEAT_TICKS: u32 = 60 * TICKS_PER_SECOND;
/// Ticks an extractor stays out after cutting out (design section 10).
pub const CUT_OUT_TICKS: u32 = 20 * TICKS_PER_SECOND;
/// Ticks a vent pauses extraction (design section 10).
pub const VENT_TICKS: u32 = 5 * TICKS_PER_SECOND;
/// Ticks an idle extractor takes to cool from full heat on its own.
pub const PASSIVE_COOL_TICKS: u32 = 90 * TICKS_PER_SECOND;

/// Tolerance for sums of per-tick amounts, which float rounding leaves just
/// short of a whole.
const EPSILON: f32 = 1e-3;

/// What an extractor is doing.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ExtractorState {
    #[default]
    Off,
    /// Working the deposit it was started on.
    On { deposit: usize },
    /// Shedding heat; resumes work on `deposit` afterwards if it was on.
    Venting {
        ticks_left: u32,
        deposit: Option<usize>,
    },
    /// Out after reaching full heat; switches off when the time is up.
    CutOut { ticks_left: u32 },
}

/// Why an extractor refused to start.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Refusal {
    /// Too far from the deposit, or moving too fast.
    NotAtDeposit,
    Depleted,
    CargoFull,
    /// Cutting out or venting.
    Busy,
}

/// Why a running extractor stopped.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Stop {
    Moved,
    Depleted,
    CargoFull,
    CutOut,
}

/// What the ship and its deposit are like this tick.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Conditions {
    /// Within [`EXTRACT_RANGE`] of the deposit and below [`MAX_EXTRACT_SPEED`].
    pub at_deposit: bool,
    pub remaining: f32,
    /// Whole units of cargo space left.
    pub space: u32,
    /// Units per minute.
    pub rate: f32,
    /// How much faster than normal heat builds.
    pub heat_factor: f32,
}

/// What one tick of extraction produced.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Yield {
    /// Whole units moved into cargo.
    pub units: u32,
    /// Material taken out of the deposit.
    pub taken: f32,
    pub stopped: Option<Stop>,
}

#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Extractor {
    pub state: ExtractorState,
    /// 0 (cold) to 1 (cut-out).
    pub heat: f32,
    /// Material taken but not yet a whole unit of cargo.
    carry: f32,
}

impl Extractor {
    /// Whether it is working a deposit this tick.
    #[must_use]
    pub const fn is_on(&self) -> bool {
        matches!(self.state, ExtractorState::On { .. })
    }

    /// Starts work on `deposit`.
    ///
    /// # Errors
    /// Refuses when the ship is not at the deposit, it is empty, cargo is
    /// full, or the extractor is cutting out or venting.
    pub fn start(&mut self, deposit: usize, conditions: &Conditions) -> Result<(), Refusal> {
        match self.state {
            ExtractorState::Venting { .. } | ExtractorState::CutOut { .. } => Err(Refusal::Busy),
            _ if !conditions.at_deposit => Err(Refusal::NotAtDeposit),
            _ if conditions.remaining <= EPSILON => Err(Refusal::Depleted),
            _ if conditions.space == 0 => Err(Refusal::CargoFull),
            _ => {
                self.state = ExtractorState::On { deposit };
                Ok(())
            }
        }
    }

    /// Switches it off. A vent in progress continues but will not resume work.
    pub fn stop(&mut self) {
        match self.state {
            ExtractorState::On { .. } => self.state = ExtractorState::Off,
            ExtractorState::Venting { ticks_left, .. } => {
                self.state = ExtractorState::Venting {
                    ticks_left,
                    deposit: None,
                };
            }
            ExtractorState::Off | ExtractorState::CutOut { .. } => {}
        }
    }

    /// Starts a vent. Returns false while cutting out or already venting.
    pub fn vent(&mut self) -> bool {
        let deposit = match self.state {
            ExtractorState::Off => None,
            ExtractorState::On { deposit } => Some(deposit),
            ExtractorState::Venting { .. } | ExtractorState::CutOut { .. } => return false,
        };
        self.state = ExtractorState::Venting {
            ticks_left: VENT_TICKS,
            deposit,
        };
        true
    }

    /// The deposit it is working, or will resume after a vent.
    #[must_use]
    pub const fn deposit(&self) -> Option<usize> {
        match self.state {
            ExtractorState::On { deposit } => Some(deposit),
            ExtractorState::Venting { deposit, .. } => deposit,
            _ => None,
        }
    }

    /// Advances one tick under `conditions`, which describe the deposit it is
    /// working (ignored unless it is on).
    pub fn step(&mut self, conditions: &Conditions) -> Yield {
        match self.state {
            ExtractorState::Off => {
                self.heat = (self.heat - 1.0 / PASSIVE_COOL_TICKS as f32).max(0.0);
                Yield::default()
            }
            ExtractorState::CutOut { ticks_left } => {
                self.heat = (self.heat - 1.0 / CUT_OUT_TICKS as f32).max(0.0);
                self.state = if ticks_left > 1 {
                    ExtractorState::CutOut {
                        ticks_left: ticks_left - 1,
                    }
                } else {
                    self.heat = 0.0;
                    ExtractorState::Off
                };
                Yield::default()
            }
            ExtractorState::Venting {
                ticks_left,
                deposit,
            } => {
                self.heat = (self.heat - 1.0 / VENT_TICKS as f32).max(0.0);
                self.state = if ticks_left > 1 {
                    ExtractorState::Venting {
                        ticks_left: ticks_left - 1,
                        deposit,
                    }
                } else {
                    self.heat = 0.0;
                    deposit.map_or(ExtractorState::Off, |deposit| ExtractorState::On {
                        deposit,
                    })
                };
                Yield::default()
            }
            ExtractorState::On { .. } => self.work(conditions),
        }
    }

    fn work(&mut self, conditions: &Conditions) -> Yield {
        let stop = if !conditions.at_deposit {
            Some(Stop::Moved)
        } else if conditions.remaining <= EPSILON {
            Some(Stop::Depleted)
        } else if conditions.space == 0 {
            Some(Stop::CargoFull)
        } else {
            None
        };
        if let Some(stop) = stop {
            self.state = ExtractorState::Off;
            return Yield {
                stopped: Some(stop),
                ..Yield::default()
            };
        }

        let per_tick = conditions.rate / (60 * TICKS_PER_SECOND) as f32;
        let mut taken = per_tick.min(conditions.remaining);
        let emptied = conditions.remaining - taken <= EPSILON;
        if emptied {
            taken = conditions.remaining;
        }
        self.carry += taken;
        // The last of a deposit counts as a whole unit, so none is stranded.
        if emptied {
            self.carry = (self.carry - EPSILON).ceil().max(0.0);
        }
        // `carry` is a few units at most, so `as` loses nothing.
        let units = ((self.carry + EPSILON).floor() as u32).min(conditions.space);
        self.carry = (self.carry - units as f32).max(0.0);

        self.heat += conditions.heat_factor / HEAT_TICKS as f32;
        let stopped = if self.heat >= 1.0 - EPSILON {
            self.heat = 1.0;
            self.state = ExtractorState::CutOut {
                ticks_left: CUT_OUT_TICKS,
            };
            Some(Stop::CutOut)
        } else {
            None
        };
        Yield {
            units,
            taken,
            stopped,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn at(remaining: f32) -> Conditions {
        Conditions {
            at_deposit: true,
            remaining,
            space: BASIC_CAPACITY,
            rate: 2.0,
            heat_factor: 1.0,
        }
    }

    /// Runs `ticks` steps, drawing from a deposit, and returns the units yielded.
    fn run(extractor: &mut Extractor, conditions: &mut Conditions, ticks: u32) -> u32 {
        let mut units = 0;
        for _ in 0..ticks {
            let out = extractor.step(conditions);
            conditions.remaining -= out.taken;
            conditions.space -= out.units;
            units += out.units;
        }
        units
    }

    #[test]
    fn yields_at_its_rate_while_held_at_the_deposit() {
        let mut extractor = Extractor::default();
        let mut conditions = at(100.0);
        extractor.start(0, &conditions).unwrap();
        // 2 per minute: one unit every 30 seconds, just before full heat.
        let units = run(&mut extractor, &mut conditions, 29 * TICKS_PER_SECOND);
        assert_eq!(units, 0);
        let units = run(&mut extractor, &mut conditions, TICKS_PER_SECOND);
        assert_eq!(units, 1);
    }

    #[test]
    fn an_unattended_extractor_cuts_out_and_stays_off() {
        let mut extractor = Extractor::default();
        let mut conditions = at(100.0);
        extractor.start(0, &conditions).unwrap();
        let mut cut_out_at = None;
        for tick in 0..HEAT_TICKS + 10 {
            if extractor.step(&conditions).stopped == Some(Stop::CutOut) {
                cut_out_at = Some(tick);
            }
        }
        // Within a tick: per-tick heat does not sum exactly in floating point.
        assert!(cut_out_at.is_some_and(|tick| tick.abs_diff(HEAT_TICKS - 1) <= 1));
        // Through the cut-out and long after, it yields nothing.
        let units = run(&mut extractor, &mut conditions, CUT_OUT_TICKS * 10);
        assert_eq!(units, 0);
        assert_eq!(extractor.state, ExtractorState::Off);
        assert_eq!(extractor.heat, 0.0);
    }

    #[test]
    fn venting_pauses_clears_heat_and_resumes() {
        let mut extractor = Extractor::default();
        let mut conditions = at(100.0);
        extractor.start(0, &conditions).unwrap();
        run(&mut extractor, &mut conditions, 40 * TICKS_PER_SECOND);
        assert!(extractor.heat > 0.6);
        assert!(extractor.vent());
        assert!(!extractor.vent(), "already venting");
        let taken_before = conditions.remaining;
        run(&mut extractor, &mut conditions, VENT_TICKS);
        assert_eq!(conditions.remaining, taken_before, "no yield while venting");
        assert_eq!(extractor.heat, 0.0);
        assert_eq!(extractor.state, ExtractorState::On { deposit: 0 });
    }

    #[test]
    fn heat_builds_faster_with_a_hazard_or_pocket() {
        let mut extractor = Extractor::default();
        let mut conditions = Conditions {
            heat_factor: 2.0,
            ..at(100.0)
        };
        extractor.start(0, &conditions).unwrap();
        run(&mut extractor, &mut conditions, HEAT_TICKS / 2);
        assert!(matches!(extractor.state, ExtractorState::CutOut { .. }));
    }

    #[test]
    fn stops_when_moved_full_or_empty() {
        let mut extractor = Extractor::default();
        extractor.start(0, &at(100.0)).unwrap();
        let moved = Conditions {
            at_deposit: false,
            ..at(100.0)
        };
        assert_eq!(extractor.step(&moved).stopped, Some(Stop::Moved));
        assert_eq!(extractor.state, ExtractorState::Off);

        extractor.start(0, &at(100.0)).unwrap();
        let full = Conditions {
            space: 0,
            ..at(100.0)
        };
        assert_eq!(extractor.step(&full).stopped, Some(Stop::CargoFull));

        extractor.start(0, &at(100.0)).unwrap();
        assert_eq!(extractor.step(&at(0.0)).stopped, Some(Stop::Depleted));
    }

    #[test]
    fn empties_a_deposit_into_whole_units() {
        let mut extractor = Extractor::default();
        let mut conditions = Conditions {
            heat_factor: 0.0,
            rate: 60.0,
            ..at(2.5)
        };
        extractor.start(0, &conditions).unwrap();
        let units = run(&mut extractor, &mut conditions, 10 * TICKS_PER_SECOND);
        assert_eq!(units, 3, "2.5 units, the last rounded up");
        assert!(conditions.remaining <= 1e-4);
    }

    #[test]
    fn refuses_to_start_away_from_a_deposit_or_while_busy() {
        let mut extractor = Extractor::default();
        let away = Conditions {
            at_deposit: false,
            ..at(10.0)
        };
        assert_eq!(extractor.start(0, &away), Err(Refusal::NotAtDeposit));
        assert_eq!(extractor.start(0, &at(0.0)), Err(Refusal::Depleted));
        let full = Conditions {
            space: 0,
            ..at(10.0)
        };
        assert_eq!(extractor.start(0, &full), Err(Refusal::CargoFull));
        assert!(extractor.vent());
        assert_eq!(extractor.start(0, &at(10.0)), Err(Refusal::Busy));
    }
}
