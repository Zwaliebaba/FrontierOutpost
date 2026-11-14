use serde::{Deserialize, Serialize};
use ts_rs::TS;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct WingmanHire {
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rig: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(
    tag = "type",
    rename_all = "camelCase",
    rename_all_fields = "camelCase"
)]
#[ts(export)]
pub enum WingmanOrder {
    Follow,
    Hold { x: i32, z: i32 },
    Return,
    Extract { deposit: u32 },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct WingmanInfo {
    pub ship_id: u16,
    pub order: WingmanOrder,
    pub state: String,
    pub cargo: u32,
    pub capacity: u32,
}
