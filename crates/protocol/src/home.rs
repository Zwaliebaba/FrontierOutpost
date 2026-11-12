//! The home's wire types (design sections 2, 6 and 7): the outpost's rules as
//! a catalog, sent once after signing in, and the pilot's home as it stands,
//! sent whenever it changes. The client draws previews from the catalog; the
//! server decides every action.

use serde::{Deserialize, Serialize};
use ts_rs::TS;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub enum Good {
    Ore,
    Salvage,
    Volatiles,
    Alloy,
    Components,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct GoodAmount {
    pub good: Good,
    pub amount: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct CostInfo {
    pub credits: u32,
    pub goods: Vec<GoodAmount>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct SocketRuleInfo {
    pub id: String,
    pub name: String,
    /// The part of the station it sits beside.
    pub beside: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct TierInfo {
    pub cost: CostInfo,
    pub draw: u32,
    /// What this tier adds, by name: the pockets, rigs and modules it opens.
    pub opens: Vec<String>,
    /// Seconds the processor takes over a batch while a facility runs at
    /// this tier, when the tier changes that.
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub batch_seconds: Option<u32>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct FacilityInfo {
    pub id: String,
    pub name: String,
    /// Tier 1 first.
    pub tiers: Vec<TierInfo>,
    /// The rig that comes with the first purchase, by fitting identifier.
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rig: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct AdjacencyInfo {
    pub facility: String,
    pub socket: String,
    /// What it gains there, as a phrase: "processes batches in 20% less time".
    pub bonus: String,
    /// The percentage it takes off the processor's time for a batch there.
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub processor_faster: Option<u32>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub enum Slot {
    Rig,
    Module,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct FittingInfo {
    pub id: String,
    pub name: String,
    pub slot: Slot,
    /// The capability it needs, by name.
    pub needs: String,
    /// The NPC price, before any discount.
    pub price: u32,
    /// What it does, as a phrase.
    pub effect: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct PriceInfo {
    pub good: Good,
    /// What NPCs pay a pilot who sells.
    pub buys_at: u32,
    /// What a pilot pays NPCs to buy.
    pub sells_at: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct RecipeInfo {
    pub id: String,
    pub input: GoodAmount,
    pub output: GoodAmount,
}

/// The outpost's rules and the posted prices (design section 10), for the
/// client's previews.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct Catalog {
    pub power_cap: u32,
    /// Seconds the core processor takes over a batch.
    pub batch_seconds: u32,
    pub sockets: Vec<SocketRuleInfo>,
    pub facilities: Vec<FacilityInfo>,
    pub adjacency: Vec<AdjacencyInfo>,
    pub reinstall: CostInfo,
    pub fittings: Vec<FittingInfo>,
    pub prices: Vec<PriceInfo>,
    pub recipes: Vec<RecipeInfo>,
    /// Launch supplies, credits per pilot.
    pub supplies: u32,
}

/// A socket of the pilot's outpost as it stands.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct SocketState {
    pub id: String,
    /// Whether it can take a facility.
    pub open: bool,
    /// The facility installed, by identifier.
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub facility: Option<String>,
    /// The tier it runs at; 0 when unpowered or empty.
    pub run: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct UnlockInfo {
    pub facility: String,
    pub tier: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct OutpostInfo {
    pub sockets: Vec<SocketState>,
    /// Every facility tier bought, installed or not.
    pub unlocks: Vec<UnlockInfo>,
    pub power_used: u32,
    /// What the powered layout opens on the expedition, by name.
    pub opens: Vec<String>,
    /// Seconds the processor takes over a batch with this layout.
    pub batch_seconds: u32,
    /// Percent off rigs and modules.
    pub fitting_discount: u32,
    /// The highest threat tier opened.
    pub open_tiers: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct QueuedInfo {
    pub recipe: String,
    /// Batches at the processor, the one being worked included.
    pub batches: u32,
}

/// The batch the processor is working, as the account message was written.
/// The client counts on from there by its own clock; the server finishes the
/// batch and sends the account again when its time has come.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct WorkingInfo {
    pub recipe: String,
    /// Seconds of work done, of the layout's `batchSeconds`.
    pub elapsed_seconds: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct MissingInfo {
    pub fitting: String,
    /// Why it stays behind, as a phrase: "lost; 60 cr to replace".
    pub reason: String,
    /// Credits to buy one now, when buying is what it needs.
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub price: Option<u32>,
}

/// What the pilot's supplies for a launch come to as their home stands
/// (design section 10).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(
    tag = "type",
    rename_all = "camelCase",
    rename_all_fields = "camelCase"
)]
#[ts(export)]
pub enum SuppliesDue {
    /// Launching takes this many credits: the posted price, or all the pilot
    /// has when credits and goods together fall short of it.
    Pays { credits: u32 },
    /// Supplies are still aboard from a run that docked without the contract
    /// being paid: launching costs nothing.
    Carried,
    /// The pilot is this many credits short and holds goods that cover it:
    /// the launch is refused until they sell.
    Short { missing: u32 },
}

/// What launching now would carry and risk (design section 6).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct LaunchInfo {
    /// Fittings that fly, by identifier; the basic fitting when empty.
    pub flown: Vec<String>,
    /// Saved fittings that stay behind.
    pub missing: Vec<MissingInfo>,
    /// Replacement price of what flies.
    pub stake: u32,
    /// The posted price of supplies.
    pub supplies: u32,
    /// What this pilot's supplies come to if they launch now.
    pub due: SuppliesDue,
    pub capacity: u32,
}

/// What a pilot's home holds.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, TS)]
#[serde(rename_all = "camelCase")]
#[ts(export)]
pub struct AccountInfo {
    pub credits: u32,
    pub ore: u32,
    pub salvage: u32,
    pub volatiles: u32,
    pub alloy: u32,
    pub components: u32,
    pub outpost: OutpostInfo,
    /// Batches at the processor, in the order they are worked.
    pub processing: Vec<QueuedInfo>,
    /// The batch being worked; absent while the processor is idle.
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub working: Option<WorkingInfo>,
    /// Rigs and modules owned, by fitting identifier, one entry per piece.
    pub hangar: Vec<String>,
    /// The saved fitting.
    #[ts(optional)]
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rig: Option<String>,
    pub modules: Vec<String>,
    pub launch: LaunchInfo,
}
