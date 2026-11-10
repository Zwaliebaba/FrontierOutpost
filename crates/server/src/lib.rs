//! Authoritative Frontier Outpost server.
//!
//! The server owns all game state and validates every client request. It never
//! trusts client-reported positions, hits, inventory or access.

pub mod account;
pub mod auth;
pub mod caps;
pub mod cert;
pub mod config;
pub mod crews;
pub mod expedition;
pub mod home;
pub mod http;
pub mod input_queue;
pub mod ledger;
pub mod relay;
pub mod runs;
pub mod store;
pub mod transport;
pub mod wire;

use frontier_protocol::{ClientMessage, PROTOCOL_VERSION, ServerMessage};

/// Answers a client's opening message.
#[must_use]
pub fn handshake(message: &ClientMessage) -> ServerMessage {
    match *message {
        ClientMessage::Lock { .. }
        | ClientMessage::Register { .. }
        | ClientMessage::SignIn { .. }
        | ClientMessage::PasskeyCredential { .. }
        | ClientMessage::Resume { .. }
        | ClientMessage::Launch { .. }
        | ClientMessage::Extract { .. }
        | ClientMessage::Vent
        | ClientMessage::Scan
        | ClientMessage::Install { .. }
        | ClientMessage::Upgrade { .. }
        | ClientMessage::Remove { .. }
        | ClientMessage::Power { .. }
        | ClientMessage::Sell { .. }
        | ClientMessage::Buy { .. }
        | ClientMessage::Process { .. }
        | ClientMessage::Unqueue { .. }
        | ClientMessage::BuyFitting { .. }
        | ClientMessage::SaveFitting { .. }
        | ClientMessage::CreateCrew
        | ClientMessage::JoinCrew { .. }
        | ClientMessage::LeaveCrew
        | ClientMessage::SetSplit { .. } => ServerMessage::Rejected {
            protocol_version: PROTOCOL_VERSION,
            reason: "the first message must be hello".into(),
        },
        ClientMessage::Hello { protocol_version } if protocol_version == PROTOCOL_VERSION => {
            ServerMessage::Welcome {
                protocol_version: PROTOCOL_VERSION,
            }
        }
        ClientMessage::Hello { protocol_version } => ServerMessage::Rejected {
            protocol_version: PROTOCOL_VERSION,
            reason: format!(
                "client speaks protocol {protocol_version}, server speaks {PROTOCOL_VERSION}"
            ),
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_the_current_protocol() {
        let reply = handshake(&ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION,
        });
        assert_eq!(
            reply,
            ServerMessage::Welcome {
                protocol_version: PROTOCOL_VERSION
            }
        );
    }

    #[test]
    fn rejects_other_protocols() {
        let reply = handshake(&ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION + 1,
        });
        assert!(matches!(reply, ServerMessage::Rejected { .. }));
    }
}
