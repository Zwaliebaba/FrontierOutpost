#pragma once

#include "Color.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

// The state the main page reads, and nothing else.
//
// This is the model in Design/Screens/README.md "State", written out as C++ aggregates. It is
// CLIENT-SIDE VIEW STATE: what the server has told this player, in the shape the screen needs it.
// It holds no rules -- it cannot advance a tick, resolve a combat or decide whether a proposal is
// still valid, and the day any of those appear here the server has stopped being authoritative
// (ADR-005, ADR-007). The same reasoning that keeps ShipView on this side of the seam keeps this
// here, one screen larger.
//
// R8: every type below is a public aggregate handed to a renderer, so plain camelCase fields and
// brace initialization. R9: it is game vocabulary, so it is Frontier and not Neuron -- the engine
// draws rectangles and glyphs and has no idea what a trade lane is.
//
// Nothing here is a wire record. When the server exists these become the client's decode target
// and Neuron/Protocol.h grows the records that fill them; the layouts below are chosen to read
// well at the call site rather than to serialize.

/// Who owns a thing: a player index, or `NOBODY`.
///
/// It was a four-value enum -- You, Halvorsen, Sorne, Neutral -- until 2026-09-11, which was right
/// for the design reference's three-empire drawing and wrong for a match of six to twelve. The
/// thing an enum could not express is that **"you" is relative to the viewer**: the same system is
/// blue on one player's screen and somebody else's colour on another's, and with the answer baked
/// into the state there was nowhere for that to live (ADR-027).
using OwnerId = std::int32_t;

inline constexpr OwnerId NOBODY = -1;

/// The colour a viewer sees an owner in.
///
/// Blue is always *you* and never anybody else, because blue is also accept and also a trade lane,
/// and the screen's whole economy is that a player learns one colour for both. Every rival takes
/// its colour from the owner's player index, so a given rival looks the same to everybody who is
/// not them.
[[nodiscard]] Neuron::Color OwnerColor(OwnerId _owner, OwnerId _viewer) noexcept;

/// How many distinct rival colours exist -- twelve, the one-pager's maximum player count.
[[nodiscard]] std::int32_t RivalColorCount() noexcept;

/// One player, as the screen names and colours them.
///
/// `label` is a short string rather than an id because the legend and the panels both want
/// something to print. It is "P2" until identity exists -- names belong to the server and arrive in
/// `4X-02` Step 3 -- and this is the one place that changes when they do.
struct PlayerBadge
{
  std::string label;
  bool isYou = false;
  std::uint32_t score = 0;
  std::uint32_t placement = 0;
  bool custodian = false;
  /// The tick they went into custody, or 0. The one-pager flags custodians on every player's map.
  std::uint32_t custodianSince = 0;
};

/// What a digest event is about. It selects the dot colour and nothing else -- the title and
/// detail are authored strings, because the digest is prose the server writes, not a template the
/// client fills in.
enum class EventKind : std::uint8_t
{
  Contact,
  Proposal,
  Loss,
  Custodian,
  Region,
  Economy,
  Ignored
};

/// What a digest event points at, so tapping it can focus the map (README "Interactions").
/// An index of NONE means the event has nothing to focus, which is a real case: the production
/// summary is about the whole empire.
struct EventRefs
{
  static constexpr std::int32_t NONE = -1;

  std::int32_t system = NONE;
  std::int32_t lane = NONE;
  std::int32_t fleet = NONE;
};

/// What tapping an action on a digest event does.
///
/// **The digest is the order surface now** (ADR-034, SCREENS.md 01), so this is the complete
/// vocabulary of what an event can offer. It is deliberately short: every entry here is something
/// the client can already carry out, because a button that draws and does nothing is worse than a
/// button that is not there. The design also asks for REBUILD LANE, PLAN ROUTE, WITHDRAW and
/// HOLD FIRE -- all four are outgoing signals, which `MatchState` cannot express at all yet, and
/// they arrive when it can.
enum class EventActionKind : std::uint8_t
{
  /// Focus the map on what this event is about.
  Focus,
  /// Open this fleet's destination picker, lane-constrained.
  RedirectFleet,
  /// Queue a build. An order: local until the lock.
  QueueBuild,
  AcceptProposal,
  DeclineProposal
};

/// One button on an event.
struct EventAction
{
  std::string label;
  EventActionKind kind = EventActionKind::Focus;
  /// An index into whatever the kind names: a fleet, a build row, a proposal.
  std::int32_t target = EventRefs::NONE;
  /// The one filled button on an event, at most (DESIGN-GUIDELINES "Components"). Everything else
  /// is outlined, so a glance finds the thing the digest thinks you should do.
  bool primary = false;
};

struct DigestEvent
{
  EventKind kind;
  std::string title;
  std::string detail;
  EventRefs refs;

  /// Who this event is about, when it is about somebody. `NOBODY` for the ones that are about the
  /// world rather than a player -- income, the region's timer. Actor grouping is built on it.
  OwnerId actor = NOBODY;

  /// The combat verdict, when the player has a fleet flying into this.
  ///
  /// Two strings because the design draws two lines and colours them differently: the verdict
  /// itself (`FLT3 ARRIVES T47 - YOU LOSE`) in amber, and the numbers under it in muted text. The
  /// numbers always say whose ships remain, which is the half the old `14 v 11 - 6 left` phrase
  /// could not (`SnapshotFleet::previewTheirsAfter`).
  std::string verdict;
  std::string verdictDetail;

  std::vector<EventAction> actions;
};

/// A system's standing flags. Bitwise rather than an enum per state because they combine: a
/// capital can be contested, and a captured system can be a custodian's.
enum class SystemFlags : std::uint8_t
{
  None = 0,
  Capital = 1 << 0,
  Contested = 1 << 1,
  /// Not a star: the anchor the sealed region is drawn around and that lanes to the frontier
  /// terminate at. It carries a position and lanes and draws no node, which is exactly what the
  /// reference map shows (two lanes running into the region, no system there).
  RegionAnchor = 1 << 2
};

[[nodiscard]] constexpr SystemFlags operator|(SystemFlags _a, SystemFlags _b) noexcept
{
  return static_cast<SystemFlags>(static_cast<std::uint8_t>(_a) | static_cast<std::uint8_t>(_b));
}
[[nodiscard]] constexpr bool HasFlag(SystemFlags _flags, SystemFlags _wanted) noexcept
{
  return (static_cast<std::uint8_t>(_flags) & static_cast<std::uint8_t>(_wanted)) != 0;
}

/// A node of the galaxy graph.
///
/// `positionX/Y` are in the map's 800x560 DESIGN space, not in screen pixels. The projection in
/// MapProjection.h is the only thing that turns one into the other, and it is applied at draw
/// time -- so a pan, a zoom or a different pane size changes where a system appears without
/// touching the state.
struct SystemNode
{
  /// The id the simulation knows this by. Position in the vector was enough while this came from a
  /// hand-typed fixture; a snapshot is fogged, so the tenth system a player can see is not system
  /// ten, and an order naming a position would name the wrong thing.
  std::int32_t id = EventRefs::NONE;
  std::string name;
  OwnerId owner = NOBODY;
  float positionX = 0.0F;
  float positionY = 0.0F;
  SystemFlags flags = SystemFlags::None;
  /// The tick a custodian went absent, or 0. The one-pager flags custodians on every player's map
  /// with the tick, because the territory is a public race rather than a private farm.
  std::uint32_t custodianSince = 0;
  /// The tick this system was captured, or 0.
  std::uint32_t capturedAt = 0;
};

/// What a lane is to this player. A lane's KIND is a diplomatic fact, not a graph fact: the same
/// edge is plain, proposed or trading depending on what its two owners have agreed (one-pager,
/// decision 3).
enum class LaneKind : std::uint8_t
{
  None,
  Trade,
  Proposed
};

struct Lane
{
  std::int32_t id = EventRefs::NONE;
  std::int32_t a = 0;
  std::int32_t b = 0;
  /// Ticks to traverse. Authored at generation, never derived from distance (one-pager).
  std::uint32_t cost = 1;
  LaneKind kind = LaneKind::None;
};

struct Graph
{
  std::vector<SystemNode> systems;
  std::vector<Lane> lanes;

  [[nodiscard]] std::int32_t FindSystem(std::string_view _name) const noexcept;
};

/// Whether a fleet is going somewhere or staying put.
///
/// Named a stance rather than an order because `GameLogic` has a `FleetOrder` and it is a different
/// thing: that one is an instruction a player gave, this one is a state the screen draws. The two
/// met for the first time in `SnapshotView.cpp` and one of them had to move.
enum class FleetStance : std::uint8_t
{
  Move,
  Hold
};

/// A fleet, in transit or holding.
///
/// A fleet in transit is PUBLIC: the one-pager makes commitment blind at the moment of choice and
/// visible afterwards, so `from`, `to`, `progress` and `eta` are things every player can see once
/// the fleet has departed. Nothing here is hidden state.
struct Fleet
{
  std::int32_t id = EventRefs::NONE;
  std::string name;
  OwnerId owner = NOBODY;
  std::uint32_t ships = 0;
  std::int32_t from = 0;
  std::int32_t to = 0;
  /// 0 at `from`, 1 at `to`. Where the arrowhead hovers over the lane.
  float progress = 0.0F;
  std::uint32_t eta = 0;
  FleetStance order = FleetStance::Hold;
  /// The deterministic engagement preview, from visible information only (one-pager: combat is
  /// deterministic; the client previews what it can see). Empty when there is nothing to fight.
  std::string preview;
  /// "in transit", "incumbent - defender bonus" -- the status line under the fleet's name.
  std::string status;
};

/// A row of the build list. A trade lane is one of these and not a diplomacy screen: it is a
/// building with two owners, so it sits next to shipyards and mining stations with *Propose*
/// where *Build* would be (one-pager, "Diplomacy UI").
struct BuildRow
{
  std::string title;
  std::string detail;
  /// What this row would build, and where. A row the player can queue has to be able to become a
  /// `BuildOrder`, and a title is not an instruction.
  std::int32_t system = EventRefs::NONE;
  /// 0 shipyard, 1 mining station. Matches `Frontier::BuildKind`, which this header cannot name --
  /// `MatchState` is the client's and `GameLogic` is the server's (AGENTS.md §2).
  std::uint8_t kind = 0;
  /// True for the trade-lane row: dashed amber border, outlined amber PROPOSE button, and it
  /// stays that way until the neighbour accepts.
  bool isTradeLane = false;
  bool available = true;
};

enum class ProposalType : std::uint8_t
{
  OpenLane,
  ShareScouting,
  HoldForTicks
};

/// An offer from another player. It is an ORDER: it locks with the others, and so does answering
/// it (one-pager, decision 3).
struct Proposal
{
  std::int32_t id = EventRefs::NONE;
  std::string from;
  ProposalType type = ProposalType::OpenLane;
  /// What the offer says, in the server's words.
  std::string terms;
  /// Counts down to four; at zero an unanswered proposal is reported to the proposer as ignored.
  std::uint32_t ticksLeft = 0;
  /// Which lane opens if this is accepted -- the conditional order that saves a second round trip.
  std::int32_t conditionalLane = EventRefs::NONE;
};

/// What the player has decided this tick and has not yet committed.
///
/// `locked` is the whole of the tick discipline on the client: edits are local until the lock, and
/// at the lock all three columns go together. Nothing here is sent early and nothing is sent
/// separately.
struct Orders
{
  std::vector<BuildRow> builds;
  /// How many builds the player could start, across the whole empire. The rail shows the few that
  /// are worth offering here; this is the total the server reports. The same distinction as
  /// `totalSystems` below and for the same reason -- what is listed is a selection, not a census.
  std::uint32_t availableBuilds = 0;
  /// Which proposal index the player has answered, and how. NONE means unanswered.
  std::int32_t answeredProposal = EventRefs::NONE;
  bool acceptedProposal = false;
  /// Which build rows the player has queued this tick, as indices into `builds`.
  std::vector<std::int32_t> queuedBuilds;
  bool locked = false;
};

struct Region
{
  /// Index of the RegionAnchor system the region is drawn around.
  std::int32_t anchor = EventRefs::NONE;
  std::uint32_t opensAt = 0;
  /// Settlement sites, as offsets from the anchor in projected pixels at scale 1. They are
  /// illustrative geometry (README "Fidelity"), unlike the projection, which is the spec.
  std::vector<std::pair<float, float>> siteOffsets;
};

struct Leader
{
  std::string name;
  std::uint32_t score = 0;
};

struct PlayerStanding
{
  std::uint32_t score = 0;
  std::uint32_t placement = 0;
  std::uint32_t playerCount = 0;
  Leader leader;
};

/// The line along the top of the screen: which match, which day, which tick, how long left.
///
/// `MatchHeader` rather than `Match`, because `GameLogic::Match` is the authoritative state of a
/// whole match and this is six fields a top bar draws. Sharing the name was harmless while the two
/// never met in one file.
struct MatchHeader
{
  std::string id;
  std::uint32_t day = 0;
  std::uint32_t totalDays = 0;
  std::string endsAt;
  /// The last RESOLVED tick. The digest is this tick's; orders are for tick + 1.
  std::uint32_t tick = 0;
  /// Seconds until the next fixed UTC tick. Counted down live; at zero the orders lock.
  double secondsToLock = 0.0;

  /// Whether the match is over -- the length ran out, or somebody was dominant long enough.
  ///
  /// **It has always been on the wire and the client ignored it** (`Snapshot::IsFinished`). The
  /// cost of ignoring it was not cosmetic: the screen went on offering buttons for a tick that
  /// would never resolve, and every tap was counted by the server as an order edit, which is the
  /// number H4 is measured from.
  bool finished = false;
};

/// Everything the main page reads.
struct MatchState
{
  /// Whether the client can currently reach the server.
  ///
  /// It is on the state because it has to be on the SCREEN. A player whose orders are silently not
  /// arriving will keep giving them, discover at the lock that nothing happened, and have no way to
  /// tell a bug from a network -- and at an hourly tick they lose an hour finding out.
  bool connected = true;

  /// Whose screen this is. Every "you" on it is relative to this.
  OwnerId viewer = 0;

  MatchHeader match;
  PlayerStanding player;
  /// Every player in the match, indexed by `OwnerId`.
  std::vector<PlayerBadge> players;
  std::vector<DigestEvent> digest;

  /// How many ticks resolved while this client had nothing on the screen.
  ///
  /// **It is a fact about this SESSION and cannot be more than that.** R13 leaves the client
  /// nothing to write, so a restarted client has no memory of what it had already read and always
  /// opens at zero. Within a session it is exact: the composition root notices when a new state
  /// arrives more than one tick after the last one it drew, which is what happens after a
  /// disconnection, a closed lid, or a night's sleep.
  std::uint32_t unreadTicks = 0;
  /// The tick this client last had on the screen. Meaningless when `unreadTicks` is zero.
  std::uint32_t lastSeenTick = 0;
  Graph graph;
  std::vector<Fleet> fleets;
  Orders orders;
  std::vector<Proposal> proposals;
  Region region;

  /// Totals for the map overlay. Held rather than counted from `graph`, because the graph the
  /// client has is what it can SEE and the totals are what the server reports -- 41 systems on a
  /// map showing 11 is not an inconsistency, it is fog.
  std::uint32_t totalSystems = 0;
  std::uint32_t unclaimedSystems = 0;

  [[nodiscard]] std::uint32_t OrdersTick() const noexcept
  {
    return match.tick + 1;
  }
};

} // namespace Frontier
