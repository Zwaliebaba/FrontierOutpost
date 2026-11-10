//! Input datagrams: the client's controls, sent every tick.
//!
//! Inputs are sent unreliably and often, so a lost one is replaced by the next
//! a tick later. Each carries a sequence number; the server applies the newest
//! and reports it back in [`crate::snapshot::Snapshot::input_ack`], which the
//! client uses to reconcile its prediction.
//!
//! Layout, little-endian, 5 bytes: kind [`INPUT_KIND`] `u8`, sequence `u16`,
//! thrust `i8`, turn `i8`. Thrust and turn are -1..=1 scaled to -127..=127.

/// First byte of every input datagram.
pub const INPUT_KIND: u8 = 2;
pub const INPUT_BYTES: usize = 5;
const SCALE: f32 = 127.0;

/// One tick of controls from the client.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InputDatagram {
    /// Increases by one per tick and wraps; compare with [`is_newer`].
    pub sequence: u16,
    pub thrust: i8,
    pub turn: i8,
}

/// Why an input datagram could not be decoded.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum InputError {
    #[error("not an input datagram")]
    WrongKind,
    #[error("input datagram is {0} bytes, not {INPUT_BYTES}")]
    WrongLength(usize),
}

impl InputDatagram {
    #[must_use]
    pub fn encode(&self) -> [u8; INPUT_BYTES] {
        let [s0, s1] = self.sequence.to_le_bytes();
        [
            INPUT_KIND,
            s0,
            s1,
            self.thrust.to_le_bytes()[0],
            self.turn.to_le_bytes()[0],
        ]
    }

    /// # Errors
    /// Fails if the datagram is not an input or has the wrong length.
    pub fn decode(bytes: &[u8]) -> Result<Self, InputError> {
        if bytes.first() != Some(&INPUT_KIND) {
            return Err(InputError::WrongKind);
        }
        let &[_, s0, s1, thrust, turn] = bytes else {
            return Err(InputError::WrongLength(bytes.len()));
        };
        Ok(Self {
            sequence: u16::from_le_bytes([s0, s1]),
            thrust: i8::from_le_bytes([thrust]),
            turn: i8::from_le_bytes([turn]),
        })
    }

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

    #[test]
    fn round_trips() {
        let input = InputDatagram {
            sequence: 513,
            thrust: 127,
            turn: -127,
        };
        assert_eq!(InputDatagram::decode(&input.encode()).unwrap(), input);
        assert_eq!(input.encode(), [2, 1, 2, 127, 129]);
    }

    #[test]
    fn rejects_malformed_datagrams() {
        assert_eq!(InputDatagram::decode(&[]), Err(InputError::WrongKind));
        assert_eq!(
            InputDatagram::decode(&[1, 0, 0, 0, 0]),
            Err(InputError::WrongKind)
        );
        assert_eq!(
            InputDatagram::decode(&[2, 0, 0, 0]),
            Err(InputError::WrongLength(4))
        );
        assert_eq!(
            InputDatagram::decode(&[2, 0, 0, 0, 0, 0]),
            Err(InputError::WrongLength(6))
        );
    }

    #[test]
    fn controls_quantise_and_read_back_within_one_step() {
        for value in [-1.0, -0.5, 0.0, 0.3, 1.0] {
            let input = InputDatagram {
                sequence: 0,
                thrust: quantize_control(value),
                turn: 0,
            };
            assert!((input.thrust_unit() - value).abs() <= 0.5 / SCALE);
        }
        assert_eq!(quantize_control(5.0), 127);
        assert_eq!(quantize_control(-5.0), -127);
        assert_eq!(quantize_control(f32::NAN), 0);
        let floor = InputDatagram {
            sequence: 0,
            thrust: i8::MIN,
            turn: i8::MIN,
        };
        assert_eq!(floor.thrust_unit(), -1.0);
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
        let cases: [(u16, f32, f32); 5] = [
            (0, 0.0, 0.0),
            (1, 1.0, -1.0),
            (65_535, -1.0, 1.0),
            (300, 0.5, -0.25),
            (7, 2.0, -2.0),
        ];
        let rows = cases
            .iter()
            .map(|&(sequence, thrust, turn)| {
                let bytes = InputDatagram {
                    sequence,
                    thrust: quantize_control(thrust),
                    turn: quantize_control(turn),
                }
                .encode();
                let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
                format!("  {{ sequence: {sequence}, thrust: {thrust:?}, turn: {turn:?}, hex: \"{hex}\" }},\n")
            })
            .collect::<String>();
        let contents = format!(
            "// This file was generated by frontier-protocol's tests. Do not edit this file manually.\n\n\
             /** Controls and the input datagrams the Rust decoder expects for them. */\n\
             export const INPUT_VECTORS: {{ sequence: number; thrust: number; turn: number; hex: string }}[] = [\n{rows}];\n"
        );
        let path = std::path::Path::new(&dir).join("inputVectors.ts");
        if std::fs::read_to_string(&path).ok().as_deref() != Some(contents.as_str()) {
            std::fs::write(path, contents).unwrap();
        }
    }
}
