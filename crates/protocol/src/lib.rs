//! Messages exchanged between the browser client and the server.
//!
//! Every type here derives [`TS`]; `cargo test` regenerates the matching
//! TypeScript in `client/src/protocol/generated/`. Commit the regenerated
//! files with the Rust change. CI fails if they are stale.
//!
//! The wire encoding is JSON for now. Integers that cross the wire are at most
//! 32 bits so they map to a TypeScript `number` without loss.

use serde::{Deserialize, Serialize};
use ts_rs::TS;

/// Bumped on every incompatible change to these messages.
pub const PROTOCOL_VERSION: u32 = 1;

/// A message sent from the client to the server.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(
    tag = "type",
    rename_all = "camelCase",
    rename_all_fields = "camelCase"
)]
#[ts(export)]
pub enum ClientMessage {
    /// First message on a new connection.
    Hello { protocol_version: u32 },
}

/// A message sent from the server to the client.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(
    tag = "type",
    rename_all = "camelCase",
    rename_all_fields = "camelCase"
)]
#[ts(export)]
pub enum ServerMessage {
    /// The server accepted the client's protocol version.
    Welcome { protocol_version: u32 },
    /// The server refused the connection; the client should show `reason`.
    Rejected {
        protocol_version: u32,
        reason: String,
    },
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hello_uses_a_tagged_camel_case_shape() {
        let json = serde_json::to_string(&ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION,
        })
        .unwrap();
        assert_eq!(json, r#"{"type":"hello","protocolVersion":1}"#);
    }

    #[test]
    fn server_messages_round_trip() {
        let message = ServerMessage::Rejected {
            protocol_version: PROTOCOL_VERSION,
            reason: "upgrade your client".into(),
        };
        let json = serde_json::to_string(&message).unwrap();
        assert_eq!(
            serde_json::from_str::<ServerMessage>(&json).unwrap(),
            message
        );
    }
}
