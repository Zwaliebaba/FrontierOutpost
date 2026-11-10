//! Messages exchanged between the browser client and the server.
//!
//! Every type here derives [`TS`]; `cargo test` regenerates the matching
//! TypeScript in `client/src/protocol/generated/`. Commit the regenerated
//! files with the Rust change. CI fails if they are stale.
//!
//! Reliable messages are JSON, framed by [`frame`] on WebTransport streams.
//! Integers that cross the wire are at most 32 bits so they map to a
//! TypeScript `number` without loss.

pub mod frame;

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

/// How a browser reaches the game server's WebTransport endpoint. Served as
/// JSON over HTTP at [`CONNECT_INFO_PATH`] by the server that serves the client.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct ConnectInfo {
    /// The WebTransport URL, such as `https://127.0.0.1:4443/`.
    pub web_transport_url: String,
    /// Hex SHA-256 of the server's self-signed certificate, for browsers to
    /// pin with `serverCertificateHashes`. Absent when the certificate is
    /// signed by an authority the browser already trusts.
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub certificate_sha256: Option<String>,
}

/// Where the server publishes [`ConnectInfo`].
pub const CONNECT_INFO_PATH: &str = "/api/connect";

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
    fn connect_info_uses_camel_case_and_omits_a_missing_hash() {
        let pinned = ConnectInfo {
            web_transport_url: "https://127.0.0.1:4443/".into(),
            certificate_sha256: Some("ab".repeat(32)),
        };
        let json = serde_json::to_string(&pinned).unwrap();
        assert!(json.contains(r#""webTransportUrl":"https://127.0.0.1:4443/""#));
        assert!(json.contains(r#""certificateSha256":"#));

        let trusted = ConnectInfo {
            certificate_sha256: None,
            ..pinned
        };
        let json = serde_json::to_string(&trusted).unwrap();
        assert!(!json.contains("certificateSha256"));
        assert_eq!(serde_json::from_str::<ConnectInfo>(&json).unwrap(), trusted);
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
