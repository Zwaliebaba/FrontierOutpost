// MatchFixture.cpp -- the tick-46 match from the design reference, as data.
//
// EVERY STRING HERE IS ASCII. The font is 96 glyphs from space (Font.h), and the reference copy
// uses five characters that are not among them: the middot separator, a true minus, an en dash, a
// right arrow and a single right angle quote. The substitutions are listed in the design note and
// applied once, here, rather than at each draw call -- so the copy in this file is exactly the
// copy on the screen and a reader can diff it against the reference by eye.

#include "pch.h"
#include "MatchFixture.h"

namespace Frontier
{

namespace
{

/// Where the design reference puts each system, in the map's 800x560 design space.
///
/// These are not eyeballed from the reference: they are its projected SVG coordinates run back
/// through the inverse of MapProjection, and every one came out a round number to within a
/// hundredth. That is the check that the projection implemented here is the projection the
/// reference was drawn with.
// Who the reference's three empires are, as player ids.
//
// **The ids are chosen so the drawing still comes out in the drawing's colours.** `OwnerColor`
// gives the viewer blue and every rival the entry at its own player id, so Halvorsen takes id 0
// (amber) and Sorne id 2 (coral), which are the colours `Design/Screens/README.md` shows them in.
// The viewer is id 3, which is also fourth of twelve -- the placement the reference prints.
//
// The `_PLAYER` suffix is not decoration: this file already has a `SORNE`, and it is a system.
constexpr OwnerId HALVORSEN_PLAYER = 0;
constexpr OwnerId SORNE_PLAYER = 2;
constexpr OwnerId YOU_PLAYER = 3;
constexpr std::int32_t PLAYERS_IN_THE_REFERENCE = 12;

struct FixtureSystem
{
  const char* name;
  float x;
  float y;
  OwnerId owner;
  SystemFlags flags;
  std::uint32_t custodianSince;
  std::uint32_t capturedAt;
};

constexpr std::array<FixtureSystem, 12> FIXTURE_SYSTEMS = {{
  {"Vesk", 180.0F, 300.0F, YOU_PLAYER, SystemFlags::Capital, 0, 0},
  {"Orune", 260.0F, 220.0F, YOU_PLAYER, SystemFlags::None, 0, 0},
  {"Tamsin", 300.0F, 340.0F, YOU_PLAYER, SystemFlags::None, 0, 0},
  {"Idris", 120.0F, 200.0F, YOU_PLAYER, SystemFlags::None, 0, 0},
  {"Kepler-Reach", 380.0F, 260.0F, HALVORSEN_PLAYER, SystemFlags::Contested, 0, 0},
  {"HALVORSEN", 470.0F, 180.0F, HALVORSEN_PLAYER, SystemFlags::Capital, 0, 0},
  {"Sorne", 520.0F, 300.0F, SORNE_PLAYER, SystemFlags::None, 0, 0},
  {"Pell", 560.0F, 400.0F, SORNE_PLAYER, SystemFlags::None, 0, 45},
  {"Okonkwo", 620.0F, 120.0F, NOBODY, SystemFlags::None, 43, 0},
  {"Narth", 660.0F, 260.0F, NOBODY, SystemFlags::None, 0, 0},
  {"Dunmore", 400.0F, 420.0F, NOBODY, SystemFlags::None, 0, 0},
  // The sealed region's anchor. It carries lanes and no node; see SystemFlags::RegionAnchor.
  {"", 690.0F, 440.0F, NOBODY, SystemFlags::RegionAnchor, 0, 0},
}};

constexpr std::int32_t VESK = 0;
constexpr std::int32_t ORUNE = 1;
constexpr std::int32_t TAMSIN = 2;
constexpr std::int32_t IDRIS = 3;
constexpr std::int32_t KEPLER_REACH = 4;
constexpr std::int32_t HALVORSEN_CAPITAL = 5;
constexpr std::int32_t SORNE = 6;
constexpr std::int32_t PELL = 7;
constexpr std::int32_t OKONKWO = 8;
constexpr std::int32_t NARTH = 9;
constexpr std::int32_t DUNMORE = 10;
constexpr std::int32_t REGION_ANCHOR = 11;

/// The lane the open proposal would turn into a trade lane. Named because three different places
/// point at it: the proposal's conditional order, the digest event, and the build row.
constexpr std::int32_t PROPOSED_LANE = 3;

constexpr std::array<Lane, 14> FIXTURE_LANES = {{
  {0, VESK, ORUNE, 1, LaneKind::Trade},
  {1, VESK, TAMSIN, 1, LaneKind::None},
  {2, VESK, IDRIS, 1, LaneKind::Trade},
  {3, ORUNE, KEPLER_REACH, 2, LaneKind::Proposed},
  {4, TAMSIN, DUNMORE, 2, LaneKind::None},
  {5, KEPLER_REACH, HALVORSEN_CAPITAL, 1, LaneKind::None},
  {6, KEPLER_REACH, SORNE, 2, LaneKind::None},
  {7, HALVORSEN_CAPITAL, OKONKWO, 3, LaneKind::None},
  {8, SORNE, NARTH, 2, LaneKind::None},
  {9, SORNE, PELL, 2, LaneKind::None},
  {10, DUNMORE, PELL, 3, LaneKind::None},
  {11, PELL, REGION_ANCHOR, 3, LaneKind::None},
  {12, NARTH, REGION_ANCHOR, 4, LaneKind::None},
  {13, OKONKWO, NARTH, 3, LaneKind::None},
}};

/// 02:14:09, the countdown the reference is frozen at.
constexpr double REFERENCE_SECONDS_TO_LOCK = (2.0 * 3600.0) + (14.0 * 60.0) + 9.0;

} // namespace

MatchState MakeReferenceMatch()
{
  MatchState state;

  state.match = MatchHeader{
    .id = "0419",
    .day = 12,
    .totalDays = 21,
    .endsAt = "22 SEP 18:00Z",
    .tick = 46,
    .secondsToLock = REFERENCE_SECONDS_TO_LOCK,
  };

  state.viewer = YOU_PLAYER;

  // Twelve badges, because the reference says twelve players. Only three are ever drawn on this
  // map; the rest are score rows belonging to empires nobody has met.
  for (std::int32_t player = 0; player < PLAYERS_IN_THE_REFERENCE; ++player)
  {
    PlayerBadge badge;
    badge.isYou = player == YOU_PLAYER;
    badge.label = player == YOU_PLAYER         ? "YOU"
                  : player == HALVORSEN_PLAYER ? "HALVORSEN"
                  : player == SORNE_PLAYER     ? "SORNE"
                                               : std::format("P{}", player + 1);
    state.players.push_back(badge);
  }
  state.players[static_cast<std::size_t>(YOU_PLAYER)].score = 1284;
  state.players[static_cast<std::size_t>(YOU_PLAYER)].placement = 4;
  state.players[static_cast<std::size_t>(HALVORSEN_PLAYER)].score = 1610;
  state.players[static_cast<std::size_t>(HALVORSEN_PLAYER)].placement = 1;

  state.player = PlayerStanding{
    .score = 1284,
    .placement = 4,
    .playerCount = 12,
    .leader = Leader{.name = "HALVORSEN", .score = 1610},
  };

  state.totalSystems = 41;
  state.unclaimedSystems = 0;

  for (const FixtureSystem& source : FIXTURE_SYSTEMS)
  {
    state.graph.systems.push_back(SystemNode{
      .name = source.name,
      .owner = source.owner,
      .positionX = source.x,
      .positionY = source.y,
      .flags = source.flags,
      .custodianSince = source.custodianSince,
      .capturedAt = source.capturedAt,
    });
  }
  state.graph.lanes.assign(FIXTURE_LANES.begin(), FIXTURE_LANES.end());

  // Sorted by consequence, which is the digest's whole contract: the top event is the one that
  // changes what you do next, and it is the one that carries the coloured left border.
  state.digest = {
    DigestEvent{.kind = EventKind::Contact,
                .title = "Contact - HALVORSEN at Kepler-Reach",
                .detail = "Outpost present. Their fleet ETA T47. Ours ETA T47.",
                .refs = {.system = KEPLER_REACH}},
    DigestEvent{.kind = EventKind::Proposal,
                .title = "Proposal - HALVORSEN > you",
                .detail = "Open lane Orune-Kepler-Reach - +6/tick - 3 ticks left",
                .refs = {.system = ORUNE, .lane = PROPOSED_LANE}},
    DigestEvent{.kind = EventKind::Loss,
                .title = "Lane cancelled: system lost",
                .detail = "Tamsin-Pell. PELL captured by SORNE (siege T44-45)",
                .refs = {.system = PELL}},
    DigestEvent{.kind = EventKind::Custodian,
                .title = "OKONKWO custodian since T43",
                .detail = "Garrisons -3. Narth reachable in 2 ticks.",
                .refs = {.system = OKONKWO}},
    DigestEvent{.kind = EventKind::Region,
                .title = "Sealed region opens T60",
                .detail = "14 ticks. Shortest path from Vesk: 9.",
                .refs = {.system = REGION_ANCHOR}},
    DigestEvent{.kind = EventKind::Economy,
                .title = "Production +38 - research +4",
                .detail = "Lane income foregone: 12/tick. Shipyard Idris idle.",
                .refs = {}},
    DigestEvent{.kind = EventKind::Ignored,
                .title = "Proposal ignored - SORNE",
                .detail = "Share scouting, sent T42. No answer in 4 ticks.",
                .refs = {}},
  };

  // Three fleets, of which two are yours. The rival's is here because a fleet in transit is
  // public: it departed, so everyone sees it and its ETA (one-pager, "Shape of a game").
  state.fleets = {
    Fleet{.name = "FLT 3",
          .owner = YOU_PLAYER,
          .ships = 14,
          .from = ORUNE,
          .to = KEPLER_REACH,
          .progress = 0.6F,
          .eta = 47,
          .order = FleetStance::Move,
          .preview = "preview: 14 v 11 (+def) - 6 left",
          .status = "in transit - ETA T47"},
    Fleet{.name = "FLT 1",
          .owner = YOU_PLAYER,
          .ships = 9,
          .from = VESK,
          .to = VESK,
          .progress = 0.0F,
          .eta = 0,
          .order = FleetStance::Hold,
          .preview = "",
          .status = "incumbent - defender bonus"},
    Fleet{.name = "HALVORSEN",
          .owner = HALVORSEN_PLAYER,
          .ships = 11,
          .from = HALVORSEN_CAPITAL,
          .to = KEPLER_REACH,
          .progress = 0.5F,
          .eta = 47,
          .order = FleetStance::Move,
          .preview = "",
          .status = "in transit - ETA T47"},
  };

  state.orders.builds = {
    BuildRow{.title = "Shipyard - Idris", .detail = "-20 - ready T49", .isTradeLane = false, .available = true},
    BuildRow{.title = "Mining station - Tamsin", .detail = "-15 - +4/tick", .isTradeLane = false, .available = true},
    // The trade lane is a BUILDING with two owners, sitting in the build list next to the
    // shipyard, greyed with PROPOSE where BUILD would be. Diplomacy has no tab (one-pager).
    BuildRow{
      .title = "Trade lane - HALVORSEN", .detail = "Orune-Kepler-Reach - +6/tick - both owners", .isTradeLane = true, .available = false},
  };
  // The count in the reference's BUILDS header. Thirty-eight across the empire, of which the three
  // rows above are the ones worth offering here.
  state.orders.availableBuilds = 38;
  state.orders.locked = false;

  state.proposals = {
    Proposal{.from = "HALVORSEN",
             .type = ProposalType::OpenLane,
             .terms = "Orune-Kepler-Reach - +6/tick each - re-validated at lock",
             .ticksLeft = 3,
             .conditionalLane = PROPOSED_LANE},
  };

  state.region = Region{
    .anchor = REGION_ANCHOR,
    .opensAt = 60,
    // Illustrative, like the rest of the sample galaxy: offsets from the anchor in projected
    // pixels at scale 1, taken from the reference's three site pins.
    .siteOffsets = {{-22.7F, -16.1F}, {22.3F, 13.2F}, {10.0F, 33.4F}},
  };

  return state;
}

} // namespace Frontier
