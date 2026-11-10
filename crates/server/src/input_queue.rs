//! Orders one pilot's inputs so the server applies each exactly once, one per
//! tick, as the client's prediction assumes.
//!
//! Inputs arrive unreliably, out of order and in redundant copies. The queue
//! keeps the ones not yet applied. Each tick it applies the next in sequence;
//! if that one is lost it moves to the next that arrived, and if none has
//! arrived it repeats the last. A backlog means the pilot's inputs are
//! arriving faster than the server steps or in a burst after a stall; beyond
//! [`MAX_BACKLOG`] the oldest are dropped, so delay cannot build up.

use frontier_protocol::input::{Controls, InputDatagram, is_newer};

/// Inputs waiting beyond which the oldest are dropped.
pub const MAX_BACKLOG: usize = 3;

#[derive(Debug, Default)]
pub struct InputQueue {
    /// Waiting inputs, oldest first.
    waiting: Vec<(u16, Controls)>,
    /// The input applied on the most recent tick and its sequence.
    current: Option<(u16, Controls)>,
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

    /// Takes the input for this tick: the next waiting one, or the last again.
    /// Returns it with the sequence to acknowledge, or `None` before any input.
    pub fn take_for_tick(&mut self) -> Option<(u16, Controls)> {
        if self.waiting.len() > MAX_BACKLOG {
            let excess = self.waiting.len() - MAX_BACKLOG;
            self.waiting.drain(..excess);
        }
        if !self.waiting.is_empty() {
            self.current = Some(self.waiting.remove(0));
        }
        self.current
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
