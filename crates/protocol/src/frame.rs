//! Framing for JSON messages on a WebTransport stream.
//!
//! A stream is a sequence of bytes with no message boundaries, so each message
//! is sent as a 4-byte big-endian length followed by that many bytes of JSON.
//! This module only encodes and decodes; reading and writing belong to the
//! server and the client.

use serde::Serialize;
use serde::de::DeserializeOwned;

/// Length of the header in front of every frame.
pub const HEADER_BYTES: usize = 4;

/// Largest JSON body a peer may send. Larger frames are a protocol error, so a
/// peer cannot make the other allocate without bound.
pub const MAX_BODY_BYTES: usize = 64 * 1024;

/// Why a frame could not be encoded or decoded.
#[derive(Debug, thiserror::Error)]
pub enum FrameError {
    #[error("frame body of {0} bytes exceeds the limit of {MAX_BODY_BYTES}")]
    TooLarge(usize),
    #[error("frame body is not a valid message: {0}")]
    Json(#[from] serde_json::Error),
}

/// Encodes `message` as one frame: header and JSON body.
///
/// # Errors
/// Fails if the message cannot be serialised or its body exceeds
/// [`MAX_BODY_BYTES`].
pub fn encode<T: Serialize>(message: &T) -> Result<Vec<u8>, FrameError> {
    let body = serde_json::to_vec(message)?;
    let length = u32::try_from(body.len())
        .ok()
        .filter(|_| body.len() <= MAX_BODY_BYTES)
        .ok_or(FrameError::TooLarge(body.len()))?;
    let mut frame = Vec::with_capacity(HEADER_BYTES + body.len());
    frame.extend_from_slice(&length.to_be_bytes());
    frame.extend_from_slice(&body);
    Ok(frame)
}

/// Reads the body length from a frame header.
///
/// # Errors
/// Fails if the announced body exceeds [`MAX_BODY_BYTES`].
pub fn body_length(header: [u8; HEADER_BYTES]) -> Result<usize, FrameError> {
    let length = usize::try_from(u32::from_be_bytes(header)).unwrap_or(usize::MAX);
    if length > MAX_BODY_BYTES {
        return Err(FrameError::TooLarge(length));
    }
    Ok(length)
}

/// Decodes a frame body into a message.
///
/// # Errors
/// Fails if the body is not valid JSON for `T`.
pub fn decode<T: DeserializeOwned>(body: &[u8]) -> Result<T, FrameError> {
    Ok(serde_json::from_slice(body)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{ClientMessage, PROTOCOL_VERSION, ServerMessage};

    #[test]
    fn a_frame_is_a_big_endian_length_and_the_json_body() {
        let frame = encode(&ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION,
        })
        .unwrap();
        let body = br#"{"type":"hello","protocolVersion":1}"#;
        assert_eq!(frame[..HEADER_BYTES], (body.len() as u32).to_be_bytes());
        assert_eq!(&frame[HEADER_BYTES..], body);
    }

    #[test]
    fn frames_round_trip() {
        let message = ServerMessage::Rejected {
            protocol_version: PROTOCOL_VERSION,
            reason: "upgrade your client".into(),
        };
        let frame = encode(&message).unwrap();
        let header: [u8; HEADER_BYTES] = frame[..HEADER_BYTES].try_into().unwrap();
        let length = body_length(header).unwrap();
        assert_eq!(length, frame.len() - HEADER_BYTES);
        let decoded: ServerMessage = decode(&frame[HEADER_BYTES..]).unwrap();
        assert_eq!(decoded, message);
    }

    #[test]
    fn refuses_to_encode_an_oversized_body() {
        let message = ServerMessage::Rejected {
            protocol_version: PROTOCOL_VERSION,
            reason: "x".repeat(MAX_BODY_BYTES),
        };
        assert!(matches!(encode(&message), Err(FrameError::TooLarge(_))));
    }

    #[test]
    fn refuses_a_header_that_announces_an_oversized_body() {
        let at_limit = u32::try_from(MAX_BODY_BYTES).unwrap();
        assert_eq!(body_length(at_limit.to_be_bytes()).unwrap(), MAX_BODY_BYTES);
        assert!(matches!(
            body_length((at_limit + 1).to_be_bytes()),
            Err(FrameError::TooLarge(_))
        ));
        assert!(body_length(u32::MAX.to_be_bytes()).is_err());
    }

    #[test]
    fn rejects_a_body_that_is_not_the_expected_message() {
        assert!(decode::<ClientMessage>(br#"{"type":"goodbye"}"#).is_err());
        assert!(decode::<ClientMessage>(b"not json").is_err());
    }
}
