//! Orders one pilot's inputs so the server applies each exactly once, one per
//! tick, as the client's prediction assumes.
//!
//! Inputs arrive unreliably, out of order and in redundant copies. The queue
//! keeps the ones not yet applied. Each tick it applies the next in sequence;
//! if that one is lost it moves to the next that arrived, and if none has
//! arrived it repeats the last. A backlog means the pilot's inputs are
//! arriving faster than the server steps or in a burst after a stall; beyond
//! [`MAX_BACKLOG`] the oldest are dropped, so delay cannot build up.
//!
//! One repeat leaves one more input waiting when the late ones arrive, and
//! nothing took two in a tick, so each lost datagram or late burst added a
//! tick of delay for the rest of the run. Inputs left waiting after the take
//! are a buffer against jitter only if the queue sometimes runs dry. Where it
//! has not for [`SETTLE_TICKS`], the fewest it held in that time were never
//! needed, and that many of the oldest are skipped: one small correction for
//! the client, in place of lasting delay.
//!
//! Repeating covers a lost datagram or two. A pilot silent for
//! [`SILENT_TICKS`] is no longer steering: a hidden tab stops sending while
//! the browser still answers the keep-alive, and a ship left on its last
//! input would thrust and fire on its own for as long as that lasted. From
//! then on the queue hands out neutral controls until an input arrives.

use frontier_protocol::input::{Controls, InputDatagram, is_newer};

/// Inputs waiting beyond which the oldest are dropped.
pub const MAX_BACKLOG: usize = 3;

/// Ticks without a fresh input after which the last one is no longer
/// repeated: half a second, well past the three datagrams in a row that the
/// client's redundant copies recover.
pub const SILENT_TICKS: u32 = 10;

/// Ticks the queue must go without running dry before the inputs it kept
/// waiting throughout count as delay: five seconds.
///
/// Shorter recovers sooner from one stall but skips on every lost datagram;
/// longer leaves a lossy link a tick or two late. Modelled on the client's
/// send loop over 45 minutes, with 0 to 20 ms of jitter and 2% loss, the
/// mean age of an applied input was 113 to 136 ms before this rule, 80 ms
/// at 60 ticks with six skips a minute, 90 ms at 100 with three and a half,
/// and 98 ms at 200 with one. Jitter alone, with no loss, skips a handful of
/// times in twenty minutes at most at any of these.
pub const SETTLE_TICKS: u32 = 100;

#[derive(Debug, Default)]
pub struct InputQueue {
    /// Waiting inputs, oldest first.
    waiting: Vec<(u16, Controls)>,
    /// The input applied on the most recent tick and its sequence.
    current: Option<(u16, Controls)>,
    /// Ticks in a row that repeated `current` for want of a fresh input.
    repeats: u32,
    /// Ticks since the queue last ran dry or was drained, and the fewest
    /// inputs left waiting after a take in that time.
    settled: u32,
    spare: usize,
}

impl InputQueue {
    /// Adds the inputs in a datagram that have not been applied or queued yet.
    pub fn receive(&mut self, datagram: &InputDatagram) {
        for (sequence, controls) in datagram.numbered() {
            let applied = self
                .current
                .is_some_and(|(current, _)| !is_newer(sequence, current));
            let queued = self.waiting.iter().any(|(waiting, _)| *waiting == sequence);
            if !applied && !queued {
                self.waiting.push((sequence, controls));
            }
        }
        let base = self.current.map_or_else(
            || {
                self.waiting
                    .iter()
                    .map(|(s, _)| *s)
                    .min_by(|a, b| order(*a, *b))
            },
            |(current, _)| Some(current),
        );
        if let Some(base) = base {
            self.waiting
                .sort_by_key(|(sequence, _)| sequence.wrapping_sub(base));
        }
    }

    /// Takes the input for this tick: the next waiting one, or the last again,
    /// or neutral controls once the pilot has been silent for
    /// [`SILENT_TICKS`]. Returns it with the sequence to acknowledge, which
    /// is always the last one received, or `None` before any input.
    pub fn take_for_tick(&mut self) -> Option<(u16, Controls)> {
        if self.waiting.len() > MAX_BACKLOG {
            let excess = self.waiting.len() - MAX_BACKLOG;
            self.waiting.drain(..excess);
        }
        if self.settled >= SETTLE_TICKS {
            // Skipped just before a take, and never the last one waiting, so
            // the input taken is newer than those skipped: their redundant
            // copies, still arriving, are then known to be old.
            let skipped = self.spare.min(self.waiting.len().saturating_sub(1));
            self.waiting.drain(..skipped);
            (self.settled, self.spare) = (0, 0);
        }
        if self.waiting.is_empty() {
            self.repeats = self.repeats.saturating_add(1);
        } else {
            self.current = Some(self.waiting.remove(0));
            self.repeats = 0;
        }
        self.note_spare();
        let (sequence, controls) = self.current?;
        if self.repeats >= SILENT_TICKS {
            return Some((sequence, Controls::default()));
        }
        Some((sequence, controls))
    }
}

impl InputQueue {
    /// Notes how many inputs this tick's take left waiting: the fewest since
    /// the queue last ran dry are the ones that have only been delay.
    fn note_spare(&mut self) {
        let left = self.waiting.len();
        if left == 0 {
            // Dry: what waits from here on is doing a buffer's work.
            (self.settled, self.spare) = (0, 0);
            return;
        }
        self.spare = if self.settled == 0 {
            left
        } else {
            self.spare.min(left)
        };
        self.settled += 1;
    }
}

/// Orders sequences by which comes first, allowing for wrap-around.
fn order(a: u16, b: u16) -> std::cmp::Ordering {
    if a == b {
        std::cmp::Ordering::Equal
    } else if is_newer(a, b) {
        std::cmp::Ordering::Greater
    } else {
        std::cmp::Ordering::Less
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn controls(thrust: i8) -> Controls {
        Controls {
            thrust,
            turn: 0,
            fire: false,
            cruise: false,
        }
    }

    /// The datagram a client sends at `sequence`: that input and up to three
    /// before it, each with thrust equal to its sequence for easy checking.
    fn datagram(sequence: u16) -> InputDatagram {
        let count = (sequence + 1).min(4);
        InputDatagram {
            sequence,
            controls: (0..count)
                .map(|back| controls((sequence - back) as i8))
                .collect(),
        }
    }

    fn sequences(queue: &mut InputQueue, ticks: usize) -> Vec<Option<u16>> {
        (0..ticks)
            .map(|_| queue.take_for_tick().map(|(s, _)| s))
            .collect()
    }

    #[test]
    fn applies_each_input_once_in_order() {
        let mut queue = InputQueue::default();
        let mut applied = Vec::new();
        for sequence in 0..10 {
            queue.receive(&datagram(sequence));
            applied.push(queue.take_for_tick().unwrap().0);
        }
        assert_eq!(applied, (0..10).collect::<Vec<_>>());
    }

    #[test]
    fn recovers_a_lost_datagram_from_the_redundant_copies() {
        let mut queue = InputQueue::default();
        queue.receive(&datagram(0));
        assert_eq!(queue.take_for_tick().unwrap().0, 0);
        // Datagram 1 is lost; datagram 2 also carries input 1.
        queue.receive(&datagram(2));
        assert_eq!(sequences(&mut queue, 2), [Some(1), Some(2)]);
    }

    #[test]
    fn repeats_the_last_input_when_none_has_arrived() {
        let mut queue = InputQueue::default();
        assert_eq!(queue.take_for_tick(), None);
        queue.receive(&datagram(0));
        assert_eq!(sequences(&mut queue, 3), [Some(0), Some(0), Some(0)]);
        queue.receive(&datagram(1));
        assert_eq!(queue.take_for_tick().unwrap(), (1, controls(1)));
    }

    #[test]
    fn a_silent_pilot_stops_steering_after_half_a_second() {
        let held = Controls {
            thrust: 127,
            turn: 40,
            fire: true,
            cruise: true,
        };
        let mut queue = InputQueue::default();
        queue.receive(&InputDatagram {
            sequence: 7,
            controls: vec![held],
        });
        assert_eq!(queue.take_for_tick(), Some((7, held)), "applied");
        // The tab is hidden: nothing more arrives, but the session stays up.
        for tick in 1..SILENT_TICKS {
            assert_eq!(queue.take_for_tick(), Some((7, held)), "repeat {tick}");
        }
        for _ in 0..100 {
            assert_eq!(
                queue.take_for_tick(),
                Some((7, Controls::default())),
                "neutral, still acknowledging the last input"
            );
        }
        // The pilot is back: their input is applied as ever.
        queue.receive(&InputDatagram {
            sequence: 8,
            controls: vec![held],
        });
        assert_eq!(queue.take_for_tick(), Some((8, held)));
        assert_eq!(queue.take_for_tick(), Some((8, held)), "and repeated");
    }

    /// One input arrives and one is taken, for `ticks` ticks from `next`.
    /// Returns the inputs left waiting after each take.
    fn in_step(queue: &mut InputQueue, next: &mut u16, ticks: u32) -> Vec<usize> {
        (0..ticks)
            .map(|_| {
                queue.receive(&datagram(*next));
                *next += 1;
                queue.take_for_tick();
                queue.waiting.len()
            })
            .collect()
    }

    #[test]
    fn inputs_that_only_ever_waited_are_skipped_once_the_link_has_settled() {
        let mut queue = InputQueue::default();
        let mut next = 0;
        assert_eq!(in_step(&mut queue, &mut next, 5), [0; 5], "in step");
        // The uplink stalls for two ticks: the last input is repeated, then
        // the late ones arrive together.
        queue.take_for_tick();
        queue.take_for_tick();
        queue.receive(&datagram(next + 1));
        next += 2;
        let after = in_step(&mut queue, &mut next, SETTLE_TICKS + 5);
        assert_eq!(after[0], 2, "two ticks behind, and it once stayed so");
        assert_eq!(after[after.len() - 1], 0, "back in step");
        let behind = after.iter().filter(|&&left| left > 0).count();
        assert!(behind <= SETTLE_TICKS as usize, "{behind} ticks behind");
        // Every input since is applied as it arrives, once.
        queue.receive(&datagram(next));
        assert_eq!(queue.take_for_tick().unwrap().0, next);
    }

    #[test]
    fn a_queue_that_runs_dry_keeps_what_it_buffers() {
        // Jitter: no input one tick, two the next, over and over. The one
        // left waiting is what covers the empty tick, so it is never skipped.
        let mut queue = InputQueue::default();
        queue.receive(&datagram(0));
        queue.take_for_tick();
        let mut applied = vec![0];
        for pair in 0..(2 * SETTLE_TICKS as u16) {
            queue.take_for_tick();
            queue.receive(&datagram(2 * pair + 2));
            applied.push(queue.take_for_tick().unwrap().0);
            applied.push(queue.take_for_tick().unwrap().0);
        }
        let expected: Vec<u16> = (0..=(4 * SETTLE_TICKS as u16)).collect();
        assert_eq!(applied, expected, "every input applied, none skipped");
    }

    #[test]
    fn handles_out_of_order_and_duplicate_datagrams() {
        let mut queue = InputQueue::default();
        queue.receive(&datagram(0));
        queue.take_for_tick();
        queue.receive(&datagram(2));
        queue.receive(&datagram(1));
        queue.receive(&datagram(2));
        assert_eq!(sequences(&mut queue, 3), [Some(1), Some(2), Some(2)]);
    }

    #[test]
    fn drops_the_oldest_beyond_the_backlog_limit() {
        let mut queue = InputQueue::default();
        queue.receive(&datagram(0));
        queue.take_for_tick();
        // A stall, then a burst: inputs 1 to 8 arrive at once.
        for sequence in [4, 8] {
            queue.receive(&datagram(sequence));
        }
        let next = queue.take_for_tick().unwrap().0;
        assert_eq!(next, 8 - MAX_BACKLOG as u16 + 1);
    }

    #[test]
    fn works_across_the_sequence_wrap() {
        let mut queue = InputQueue::default();
        let mut applied = Vec::new();
        for sequence in [65_534, 65_535, 0, 1] {
            queue.receive(&InputDatagram {
                sequence,
                controls: vec![controls(0)],
            });
            applied.push(queue.take_for_tick().unwrap().0);
        }
        assert_eq!(applied, [65_534, 65_535, 0, 1]);
    }
}
