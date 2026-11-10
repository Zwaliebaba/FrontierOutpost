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
pub mod home;
pub mod input;
pub mod snapshot;

pub use home::{
    AccountInfo, AdjacencyInfo, Catalog, CostInfo, FacilityInfo, FittingInfo, Good, GoodAmount,
    LaunchInfo, MissingInfo, OutpostInfo, PriceInfo, QueuedInfo, RecipeInfo, Slot, SocketRuleInfo,
    SocketState, TierInfo, UnlockInfo,
};
use serde::{Deserialize, Serialize};
use ts_rs::TS;

/// Bumped on every incompatible change to these messages.
pub const PROTOCOL_VERSION: u32 = 7;

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
    /// Starts registering a new pilot with a passkey. The server answers with
    /// `PasskeyOptions` or `SignInRefused`.
    Register { callsign: String },
    /// Starts signing in the pilot called `callsign` with their passkey.
    SignIn { callsign: String },
    /// Finishes a registration or sign-in: `credential` is the browser's
    /// `PublicKeyCredential.toJSON()`, as JSON text.
    PasskeyCredential { credential: String },
    /// Signs in again with the token from `SignedIn`, without a passkey.
    Resume { token: String },
    /// Locks a hostile ship as the weapon target, or releases the lock with
    /// `None`. The server ignores a lock on anything but a hostile in range.
    Lock { target: Option<u16> },
    /// Accepts the offered contract at threat tier `tier` (1 to 3) and launches.
    Launch { tier: u32 },
    /// Starts the extractor at the nearest deposit in reach, or stops it.
    Extract { on: bool },
    /// Vents the extractor: a 5-second pause that clears its heat.
    Vent,
    /// Starts a scan of the site; the ship must hold still in scan range.
    Scan,
    /// Installs `facility` in the empty socket `socket`: bought at tier 1 the
    /// first time, installed again for the reinstallation cost after that.
    Install { socket: String, facility: String },
    /// Buys the next tier of the facility in `socket`.
    Upgrade { socket: String },
    /// Removes the facility in `socket`, for nothing; its unlock stays.
    Remove { socket: String },
    /// Runs the facility in `socket` at tier `run`, or unpowers it with 0.
    Power { socket: String, run: u32 },
    /// Sells `amount` of `good` to NPCs at the posted price.
    Sell { good: Good, amount: u32 },
    /// Buys `amount` of `good` from NPCs at the posted price.
    Buy { good: Good, amount: u32 },
    /// Queues `batches` of `recipe` at the processor; inputs leave the stock now.
    Process { recipe: String, batches: u32 },
    /// Takes queued batches back, returning their inputs.
    Unqueue { recipe: String, batches: u32 },
    /// Buys a rig or module.
    BuyFitting { fitting: String },
    /// Saves the fitting to fly, by fitting identifier.
    SaveFitting {
        rig: Option<String>,
        modules: Vec<String>,
    },
}

/// A message sent from the server to the client.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, TS)]
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
    /// Options for the browser's WebAuthn call, as JSON text: for a
    /// registration, `{ "publicKey": PublicKeyCredentialCreationOptionsJSON }`;
    /// for a sign-in, `{ "publicKey": PublicKeyCredentialRequestOptionsJSON }`.
    PasskeyOptions { options: String },
    /// The pilot is signed in. `token` signs them in again with `Resume`.
    /// A contract follows.
    SignedIn {
        callsign: String,
        token: String,
        account: AccountInfo,
    },
    /// Registration, sign-in or resumption failed, for `reason`.
    SignInRefused { reason: String },
    /// The pilot's home holdings changed.
    Account { account: AccountInfo },
    /// The outpost's rules and posted prices. Sent once, after `SignedIn`.
    Catalog { catalog: Catalog },
    /// An action at home was refused, for `reason`; nothing changed.
    HomeRefused { reason: String },
    /// The discovery contract on offer (design section 4): the site and what
    /// preparation shows of its seed. Sent after `Welcome` and after each run.
    Contract {
        site: SiteInfo,
        hazard: Hazard,
        dominant_threat: DominantThreat,
        tiers: Vec<TierOffer>,
    },
    /// The pilot launched and flies the ship `ship_id`. Snapshots follow as
    /// datagrams, `tick_rate` per second. Everything here is fixed for the run.
    Joined {
        /// The run's site: a page that rejoins a run has no contract for it.
        site: SiteInfo,
        ship_id: u16,
        tick_rate: u32,
        tier: u32,
        obstacles: Vec<ObstacleInfo>,
        /// The deposits the pilot can work, in the order snapshots report them.
        deposits: Vec<DepositInfo>,
        /// How close a locked target must be for the pilot's weapon to fire, m.
        weapon_range: f32,
        /// How far the pilot can lock a target, m.
        lock_range: f32,
        /// How far from the site's centre a scan reaches, m.
        scan_range: f32,
        /// How far from a deposit's centre the extractor reaches, m.
        extract_range: f32,
        /// Multiplies the starter hull's speed and acceleration: the scout
        /// rig's 20%. Prediction must use it.
        speed_factor: f32,
        /// Shield points when full; 0 without an escort rig.
        max_shield: f32,
        /// The rigs and modules flown, by fitting identifier.
        fitted: Vec<String>,
    },
    /// The launch was refused, for `reason`.
    LaunchRefused { reason: String },
    /// The extractor did not start.
    ExtractRefused { reason: ExtractRefusal },
    /// A scan revealed the site's hidden seed values (design section 10).
    Scanned {
        /// Index into the contract's pockets.
        rich_pocket: u32,
        /// The two pockets holding the relays.
        relays: Vec<u32>,
        /// The coming alert steps, as far as the instance can last.
        waves: Vec<WaveInfo>,
    },
    /// The instance ends in `seconds_left` seconds (design section 10).
    LimitWarning { seconds_left: u32 },
    /// The pilot's run is over. A new `Contract` follows.
    RunOver { summary: RunSummary },
    /// The instance ended and the crew's pot was paid: `share` of it to this
    /// pilot, by the host's split. A pilot who docked last also sees their
    /// share in `RunOver`.
    PotPaid { pot: PotInfo, share: u32 },
}

/// What a crew earned between them (design section 10).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct PotInfo {
    pub contract: u32,
    pub bounty: u32,
    pub relay: u32,
}

/// A pocket of the site, as a contract shows it.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct PocketInfo {
    pub kind: PocketKind,
    pub x: f32,
    pub z: f32,
    pub radius: f32,
    /// Whether the crew lacks the facility it needs.
    pub locked: bool,
    /// What its hidden seed values cannot change: what it yields.
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub material: Option<Material>,
}

/// The site a contract offers.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct SiteInfo {
    pub name: String,
    pub dock_x: f32,
    pub dock_z: f32,
    /// Leaving this circle after working a pocket starts the return leg.
    pub centre_x: f32,
    pub centre_z: f32,
    pub radius: f32,
    pub pockets: Vec<PocketInfo>,
    /// Entering this circle around the dock after undocking docks the ship.
    pub dock_radius: f32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub enum PocketKind {
    OreField,
    Derelict,
    VolatileVeins,
    NestCore,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub enum Material {
    Ore,
    Salvage,
    Volatiles,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub enum Hazard {
    ThermalFlare,
    DebrisDrift,
    IonFog,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub enum DominantThreat {
    Drones,
    Raiders,
    Balanced,
}

/// One threat tier of a contract.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct TierOffer {
    pub tier: u32,
    /// Credits for the whole expedition.
    pub payout: u32,
    /// Whether the pilot has opened it (design section 10: tiers open with facilities).
    pub open: bool,
}

/// A deposit the pilot can work.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct DepositInfo {
    pub x: f32,
    pub z: f32,
    /// Index into the contract's pockets.
    pub pocket: u32,
    pub material: Material,
    /// Units it holds at launch.
    pub units: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub enum ExtractRefusal {
    /// Too far from a deposit, or moving too fast.
    NotAtDeposit,
    Depleted,
    CargoFull,
    /// Cutting out or venting.
    Busy,
}

/// One coming alert step, revealed by a scan.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct WaveInfo {
    /// The alert level it brings.
    pub level: u32,
    /// Seconds since launch when time raises the alert to this level; cut-outs
    /// can bring it sooner.
    pub at_seconds: u32,
    pub drones: u32,
    pub raiders: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub enum Ending {
    Docked,
    Destroyed,
    Abandoned,
}

/// How a pilot's run ended and what it brought home (design section 6).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct RunSummary {
    pub ending: Ending,
    pub ore: u32,
    pub salvage: u32,
    pub volatiles: u32,
    /// The pilot's share of the crew's pot, credits, when it was paid as they
    /// came home; otherwise 0 and a `PotPaid` follows when the instance ends.
    pub payout: u32,
    pub launch_cost: u32,
    pub cargo_lost: u32,
    pub modules_lost: u32,
    pub approach_seconds: u32,
    pub objective_seconds: u32,
    pub return_seconds: u32,
}

/// A fixed circular obstacle on the gameplay plane, in metres.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct ObstacleInfo {
    pub x: f32,
    pub z: f32,
    pub radius: f32,
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

    /// ts-rs exports types but not constants, so the protocol version is written
    /// beside the generated types here. CI fails if the committed file is stale.
    #[test]
    fn exports_the_protocol_version_to_typescript() {
        let Ok(dir) = std::env::var("TS_RS_EXPORT_DIR") else {
            return;
        };
        let contents = format!(
            "// This file was generated by frontier-protocol's tests. Do not edit this file manually.\n\n\
             /** Bumped on every incompatible change to the wire messages. */\n\
             export const PROTOCOL_VERSION = {PROTOCOL_VERSION};\n"
        );
        let path = std::path::Path::new(&dir).join("version.ts");
        if std::fs::read_to_string(&path).ok().as_deref() != Some(contents.as_str()) {
            std::fs::write(path, contents).unwrap();
        }
    }

    #[test]
    fn hello_uses_a_tagged_camel_case_shape() {
        let json = serde_json::to_string(&ClientMessage::Hello {
            protocol_version: PROTOCOL_VERSION,
        })
        .unwrap();
        assert_eq!(
            json,
            format!(r#"{{"type":"hello","protocolVersion":{PROTOCOL_VERSION}}}"#)
        );
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
    fn lock_carries_a_target_or_null() {
        let lock = ClientMessage::Lock { target: Some(7) };
        assert_eq!(
            serde_json::to_string(&lock).unwrap(),
            r#"{"type":"lock","target":7}"#
        );
        let release: ClientMessage =
            serde_json::from_str(r#"{"type":"lock","target":null}"#).unwrap();
        assert_eq!(release, ClientMessage::Lock { target: None });
    }

    #[test]
    fn launch_and_actions_use_the_tagged_shape() {
        assert_eq!(
            serde_json::to_string(&ClientMessage::Launch { tier: 1 }).unwrap(),
            r#"{"type":"launch","tier":1}"#
        );
        assert_eq!(
            serde_json::to_string(&ClientMessage::Extract { on: true }).unwrap(),
            r#"{"type":"extract","on":true}"#
        );
        assert_eq!(
            serde_json::to_string(&ClientMessage::Vent).unwrap(),
            r#"{"type":"vent"}"#
        );
    }

    #[test]
    fn identity_messages_use_the_tagged_shape() {
        assert_eq!(
            serde_json::to_string(&ClientMessage::Register {
                callsign: "Vega".into()
            })
            .unwrap(),
            r#"{"type":"register","callsign":"Vega"}"#
        );
        let signed_in = ServerMessage::SignedIn {
            callsign: "Vega".into(),
            token: "ab".into(),
            account: AccountInfo {
                credits: 100,
                ore: 0,
                salvage: 0,
                volatiles: 0,
                alloy: 0,
                components: 0,
                outpost: OutpostInfo {
                    sockets: Vec::new(),
                    unlocks: Vec::new(),
                    power_used: 0,
                    opens: Vec::new(),
                    processor_batches: 4,
                    fitting_discount: 0,
                    open_tiers: 1,
                },
                processing: Vec::new(),
                hangar: Vec::new(),
                rig: None,
                modules: Vec::new(),
                launch: LaunchInfo {
                    flown: Vec::new(),
                    missing: Vec::new(),
                    stake: 0,
                    supplies: 40,
                    capacity: 20,
                },
            },
        };
        let json = serde_json::to_string(&signed_in).unwrap();
        assert!(json.starts_with(
            r#"{"type":"signedIn","callsign":"Vega","token":"ab","account":{"credits":100"#
        ));
        assert_eq!(
            serde_json::from_str::<ServerMessage>(&json).unwrap(),
            signed_in
        );
    }

    #[test]
    fn home_actions_use_the_tagged_shape() {
        assert_eq!(
            serde_json::to_string(&ClientMessage::Install {
                socket: "a".into(),
                facility: "refinery".into()
            })
            .unwrap(),
            r#"{"type":"install","socket":"a","facility":"refinery"}"#
        );
        assert_eq!(
            serde_json::to_string(&ClientMessage::Sell {
                good: Good::Ore,
                amount: 3
            })
            .unwrap(),
            r#"{"type":"sell","good":"ore","amount":3}"#
        );
        assert_eq!(
            serde_json::to_string(&ClientMessage::SaveFitting {
                rig: None,
                modules: vec!["cargo_frame".into()]
            })
            .unwrap(),
            r#"{"type":"saveFitting","rig":null,"modules":["cargo_frame"]}"#
        );
    }

    #[test]
    fn a_run_summary_uses_camel_case() {
        let message = ServerMessage::RunOver {
            summary: RunSummary {
                ending: Ending::Docked,
                ore: 20,
                salvage: 0,
                volatiles: 0,
                payout: 100,
                launch_cost: 40,
                cargo_lost: 0,
                modules_lost: 0,
                approach_seconds: 170,
                objective_seconds: 900,
                return_seconds: 180,
            },
        };
        let json = serde_json::to_string(&message).unwrap();
        assert!(json.starts_with(r#"{"type":"runOver","summary":{"ending":"docked","ore":20"#));
        assert!(json.contains(r#""approachSeconds":170"#));
        assert_eq!(
            serde_json::from_str::<ServerMessage>(&json).unwrap(),
            message
        );
    }

    #[test]
    fn a_contract_round_trips() {
        let message = ServerMessage::Contract {
            site: SiteInfo {
                name: "Kessler Drift".into(),
                dock_x: -900.0,
                dock_z: 0.0,
                centre_x: 620.0,
                centre_z: 0.0,
                radius: 360.0,
                pockets: vec![PocketInfo {
                    kind: PocketKind::VolatileVeins,
                    x: 820.0,
                    z: -210.0,
                    radius: 32.0,
                    locked: true,
                    material: Some(Material::Volatiles),
                }],
                dock_radius: 15.0,
            },
            hazard: Hazard::IonFog,
            dominant_threat: DominantThreat::Balanced,
            tiers: vec![TierOffer {
                tier: 1,
                payout: 100,
                open: true,
            }],
        };
        let json = serde_json::to_string(&message).unwrap();
        assert!(json.contains(r#""kind":"volatileVeins""#));
        assert!(json.contains(r#""hazard":"ionFog""#));
        assert_eq!(
            serde_json::from_str::<ServerMessage>(&json).unwrap(),
            message
        );
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
