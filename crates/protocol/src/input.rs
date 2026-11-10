//! Input datagrams: the client's controls, sent every tick.
//!
//! Inputs are sent unreliably, so each datagram carries the newest input and
//! up to [`MAX_INPUTS`] - 1 before it: a lost datagram is recovered from the
//! next one. Inputs are numbered; the server applies them in order, one per
//! tick, and reports the last one applied in
//! [`crate::snapshot::Snapshot::input_ack`], which the client uses to
//! reconcile its prediction.
//!
//! Layout, little-endian: kind [`INPUT_KIND`] `u8`, sequence of the newest
//! input `u16`, count `u8` (1 to [`MAX_INPUTS`]), then `count` pairs of thrust
//! `i8` and turn `i8`, newest first. Thrust and turn are -1..=1 scaled to
//! -127..=127.

/// First byte of every input datagram.
pub const INPUT_KIND: u8 = 2;
/// Inputs per datagram: the newest and three before it.
pub const MAX_INPUTS: usize = 4;
const HEADER_BYTES: usize = 4;
const SCALE: f32 = 127.0;

/// One tick of controls, quantised.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Controls {
    pub thrust: i8,
    pub turn: i8,
}

impl Controls {
    /// Thrust as -1..=1. -128 is read as -1.
    #[must_use]
    pub fn thrust_unit(&self) -> f32 {
        unit(self.thrust)
    }

    /// Turn as -1..=1. -128 is read as -1.
    #[must_use]
    pub fn turn_unit(&self) -> f32 {
        unit(self.turn)
    }
}

/// The newest input and the ones just before it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InputDatagram {
    /// Sequence number of `controls[0]`. Increases by one per tick and wraps;
    /// compare with [`is_newer`]. `controls[i]` has sequence `sequence - i`.
    pub sequence: u16,
    /// Newest first, 1 to [`MAX_INPUTS`] entries.
    pub controls: Vec<Controls>,
}

/// Why an input datagram could not be encoded or decoded.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum InputError {
    #[error("not an input datagram")]
    WrongKind,
    #[error("an input datagram carries 1 to {MAX_INPUTS} inputs, not {0}")]
    WrongCount(usize),
    #[error("input datagram is {actual} bytes; its header implies {expected}")]
    WrongLength { expected: usize, actual: usize },
}

impl InputDatagram {
    /// The inputs with their sequence numbers, newest first.
    pub fn numbered(&self) -> impl Iterator<Item = (u16, Controls)> + '_ {
        (0_u16..)
            .zip(&self.controls)
            .map(|(back, controls)| (self.sequence.wrapping_sub(back), *controls))
    }

    /// # Errors
    /// Fails if there are no inputs or more than [`MAX_INPUTS`].
    pub fn encode(&self) -> Result<Vec<u8>, InputError> {
        let count = self.controls.len();
        if !(1..=MAX_INPUTS).contains(&count) {
            return Err(InputError::WrongCount(count));
        }
        let mut bytes = Vec::with_capacity(HEADER_BYTES + 2 * count);
        bytes.push(INPUT_KIND);
        bytes.extend_from_slice(&self.sequence.to_le_bytes());
        // `count` is at most MAX_INPUTS, checked above.
        bytes.push(count as u8);
        for controls in &self.controls {
            bytes.extend_from_slice(&controls.thrust.to_le_bytes());
            bytes.extend_from_slice(&controls.turn.to_le_bytes());
        }
        Ok(bytes)
    }

    /// # Errors
    /// Fails if the datagram is not an input, its count is out of range, or its
    /// length does not match the count.
    pub fn decode(bytes: &[u8]) -> Result<Self, InputError> {
        if bytes.first() != Some(&INPUT_KIND) {
            return Err(InputError::WrongKind);
        }
        let &[_, s0, s1, count, ref rest @ ..] = bytes else {
            return Err(InputError::WrongLength {
                expected: HEADER_BYTES,
                actual: bytes.len(),
            });
        };
        let count = usize::from(count);
        if !(1..=MAX_INPUTS).contains(&count) {
            return Err(InputError::WrongCount(count));
        }
        if rest.len() != 2 * count {
            return Err(InputError::WrongLength {
                expected: HEADER_BYTES + 2 * count,
                actual: bytes.len(),
            });
        }
        let controls = rest
            .as_chunks::<2>()
            .0
            .iter()
            .map(|&[thrust, turn]| Controls {
                thrust: i8::from_le_bytes([thrust]),
                turn: i8::from_le_bytes([turn]),
            })
            .collect();
        Ok(Self {
            sequence: u16::from_le_bytes([s0, s1]),
            controls,
        })
    }
}

/// Scales -1..=1 to -127..=127, clamping and treating NaN as 0.
#[must_use]
pub fn quantize_control(value: f32) -> i8 {
    if value.is_nan() {
        return 0;
    }
    // The clamp keeps the product inside i8, so `as` cannot saturate here.
    (value.clamp(-1.0, 1.0) * SCALE).round() as i8
}

fn unit(value: i8) -> f32 {
    (f32::from(value) / SCALE).max(-1.0)
}

/// Whether sequence `candidate` comes after `current`, allowing for wrap-around:
/// anything up to half the sequence space ahead counts as newer.
#[must_use]
pub fn is_newer(candidate: u16, current: u16) -> bool {
    candidate.wrapping_sub(current).cast_signed() > 0
}

#[cfg(test)]
mod tests {
    use super::*;

    fn controls(thrust: i8, turn: i8) -> Controls {
        Controls { thrust, turn }
    }

    #[test]
    fn round_trips_and_numbers_inputs_newest_first() {
        let input = InputDatagram {
            sequence: 1,
            controls: vec![controls(127, -127), controls(0, 5), controls(-127, 0)],
        };
        let bytes = input.encode().unwrap();
        assert_eq!(bytes, [2, 1, 0, 3, 127, 129, 0, 5, 129, 0]);
        assert_eq!(InputDatagram::decode(&bytes).unwrap(), input);
        let numbered: Vec<u16> = input.numbered().map(|(sequence, _)| sequence).collect();
        assert_eq!(numbered, [1, 0, u16::MAX]);
    }

    #[test]
    fn refuses_to_encode_no_inputs_or_too_many() {
        let empty = InputDatagram {
            sequence: 0,
            controls: vec![],
        };
        assert_eq!(empty.encode(), Err(InputError::WrongCount(0)));
        let many = InputDatagram {
            sequence: 0,
            controls: vec![Controls::default(); MAX_INPUTS + 1],
        };
        assert_eq!(many.encode(), Err(InputError::WrongCount(MAX_INPUTS + 1)));
    }

    #[test]
    fn rejects_malformed_datagrams() {
        assert_eq!(InputDatagram::decode(&[]), Err(InputError::WrongKind));
        assert_eq!(
            InputDatagram::decode(&[1, 0, 0, 1, 0, 0]),
            Err(InputError::WrongKind)
        );
        assert!(matches!(
            InputDatagram::decode(&[2, 0, 0]),
            Err(InputError::WrongLength { .. })
        ));
        assert_eq!(
            InputDatagram::decode(&[2, 0, 0, 0]),
            Err(InputError::WrongCount(0))
        );
        assert_eq!(
            InputDatagram::decode(&[2, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
            Err(InputError::WrongCount(5))
        );
        assert!(matches!(
            InputDatagram::decode(&[2, 0, 0, 2, 0, 0, 0]),
            Err(InputError::WrongLength { .. })
        ));
    }

    #[test]
    fn controls_quantise_and_read_back_within_one_step() {
        for value in [-1.0, -0.5, 0.0, 0.3, 1.0] {
            let quantised = controls(quantize_control(value), 0);
            assert!((quantised.thrust_unit() - value).abs() <= 0.5 / SCALE);
        }
        assert_eq!(quantize_control(5.0), 127);
        assert_eq!(quantize_control(-5.0), -127);
        assert_eq!(quantize_control(f32::NAN), 0);
        assert_eq!(controls(i8::MIN, i8::MIN).thrust_unit(), -1.0);
    }

    #[test]
    fn newer_sequences_survive_wrap_around() {
        assert!(is_newer(1, 0));
        assert!(!is_newer(0, 0));
        assert!(!is_newer(0, 1));
        assert!(is_newer(0, u16::MAX));
        assert!(is_newer(10, 65_530));
        assert!(!is_newer(65_530, 10));
    }

    /// Writes vectors for the TypeScript encoder's tests beside the generated
    /// types. CI fails if the committed file is stale.
    #[test]
    fn exports_test_vectors_to_typescript() {
        let Ok(dir) = std::env::var("TS_RS_EXPORT_DIR") else {
            return;
        };
        let cases: [(u16, &[(f32, f32)]); 4] = [
            (0, &[(0.0, 0.0)]),
            (1, &[(1.0, -1.0), (0.5, -0.25)]),
            (65_535, &[(-1.0, 1.0), (2.0, -2.0), (0.0, 1.0), (1.0, 0.0)]),
            (300, &[(0.3, -0.7), (0.0, 0.0), (1.0, 1.0)]),
        ];
        let rows = cases
            .iter()
            .map(|&(sequence, inputs)| {
                let datagram = InputDatagram {
                    sequence,
                    controls: inputs
                        .iter()
                        .map(|&(thrust, turn)| {
                            controls(quantize_control(thrust), quantize_control(turn))
                        })
                        .collect(),
                };
                let hex: String = datagram
                    .encode()
                    .unwrap()
                    .iter()
                    .map(|b| format!("{b:02x}"))
                    .collect();
                let list = inputs
                    .iter()
                    .map(|(thrust, turn)| format!("{{ thrust: {thrust:?}, turn: {turn:?} }}"))
                    .collect::<Vec<_>>()
                    .join(", ");
                format!("  {{ sequence: {sequence}, inputs: [{list}], hex: \"{hex}\" }},\n")
            })
            .collect::<String>();
        let contents = format!(
            "// This file was generated by frontier-protocol's tests. Do not edit this file manually.\n\n\
             export const MAX_INPUTS = {MAX_INPUTS};\n\n\
             /** Controls, newest first, and the input datagrams the Rust decoder expects for them. */\n\
             export const INPUT_VECTORS: {{\n  sequence: number;\n  inputs: {{ thrust: number; turn: number }}[];\n  hex: string;\n}}[] = [\n{rows}];\n"
        );
        let path = std::path::Path::new(&dir).join("inputVectors.ts");
        if std::fs::read_to_string(&path).ok().as_deref() != Some(contents.as_str()) {
            std::fs::write(path, contents).unwrap();
        }
    }
}
