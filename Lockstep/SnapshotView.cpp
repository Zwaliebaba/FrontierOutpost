// SnapshotView.cpp -- a snapshot rendered as a screen, and a screen read back as orders.
//
// Two directions, and they are not symmetric on purpose. Coming in, almost everything is copied and
// a few things are composed into prose the screen needs. Going out, almost nothing is sent: only
// what the player can have changed. A client that echoed the state back would be a client asserting
// things it does not own, and the server would have to decide which parts to believe.

#include "pch.h"
#include "SnapshotView.h"

#include <algorithm>

namespace Lockstep
{

namespace
{

/// The digest kinds the screen has dots for.
///
/// `DigestKind` has sixteen values and `EventKind` has seven, because one names what happened and
/// the other selects a colour (ADR-020). Mapping many onto few is the right direction: a rule can
/// say something new without the palette having to grow a hue for it.
[[nodiscard]] EventKind ColorOf(DigestKind _kind) noexcept
{
  switch (_kind)
  {
  case DigestKind::Contact:
    return EventKind::Contact;
  case DigestKind::ProposalReceived:
  case DigestKind::ProposalAnswered:
  case DigestKind::ProposalWithdrawn:
  case DigestKind::ProposalVoided:
  case DigestKind::AgreementOpened:
    return EventKind::Proposal;
  case DigestKind::ProposalIgnored:
    return EventKind::Ignored;
  case DigestKind::SystemLost:
  case DigestKind::SiegeBegun:
  case DigestKind::Battle:
  case DigestKind::AgreementBreached:
  case DigestKind::MatchEnded:
  // A building lost with the system it stood on is a loss, and wears a loss's colour: the credits
  // are gone and there is nothing to answer (ADR-069).
  case DigestKind::BuildLost:
    return EventKind::Loss;
  case DigestKind::Custodian:
    return EventKind::Custodian;
  case DigestKind::Region:
    return EventKind::Region;
  case DigestKind::SystemClaimed:
  case DigestKind::LaneOpened:
  case DigestKind::LaneCanceled:
  case DigestKind::Economy:
  case DigestKind::OrderRejected:
  // Started, completed and a rival's rising building are all economy-coloured; what separates them
  // on the screen is the actor, which groups a rival's tell under that rival (ADR-034).
  case DigestKind::BuildStarted:
  case DigestKind::BuildCompleted:
  case DigestKind::BuildSeen:
  default:
    return EventKind::Economy;
  }
}

[[nodiscard]] SystemFlags FlagsOf(const SnapshotSystem& _system, std::uint32_t _tick)
{
  SystemFlags flags = SystemFlags::None;
  if (_system.kind == SystemKind::Capital)
  {
    flags = flags | SystemFlags::Capital;
  }
  if (_system.kind == SystemKind::RegionAnchor)
  {
    flags = flags | SystemFlags::RegionAnchor;
  }
  // A system under siege right now. A remembered one reports nothing, because a siege that was
  // under way three ticks ago may have ended (ADR-022).
  if (_system.live && _system.siegeTicks > 0)
  {
    flags = flags | SystemFlags::Contested;
  }
  (void)_tick;
  return flags;
}

[[nodiscard]] std::string NameOfPlayer(const std::vector<PlayerBadge>& _players, OwnerId _player)
{
  if (_player >= 0 && _player < static_cast<OwnerId>(_players.size()))
  {
    return _players[static_cast<std::size_t>(_player)].label;
  }
  return "SOMEBODY";
}

/// Whether a build row may be put on a card: one the lock would actually start, and not one offered
/// on another card already.
///
/// **`available` rather than `!rising`** since a system that is building composes the rows it cannot
/// take yet as well as the one it is taking (ADR-107): both are rows a button would be a lie on, and
/// one field says so for the sheet, the digest and the rail's `N AVAIL` alike.
///
/// A free function rather than a lambda inside `ViewOf`, deliberately: clang-format 18 and 22
/// disagree about where a wrapped lambda's opening brace goes, CI pins 18, and a construct the two
/// format differently is one every session has to fight.
[[nodiscard]] bool Offerable(const MatchState& _state, const std::vector<std::int32_t>& _offered, std::int32_t _row)
{
  return _state.orders.builds[static_cast<std::size_t>(_row)].available && std::ranges::find(_offered, _row) == _offered.end();
}

/// `Shipyard - Dothan` becomes `SHIPYARD DOTHAN`: a button is 8px text in a 400px column and the
/// separator costs three characters it cannot spare.
[[nodiscard]] std::string Shortened(std::string_view _title)
{
  std::string out;
  out.reserve(_title.size());
  for (const char letter : _title)
  {
    if (letter == '-')
    {
      continue;
    }
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(letter))));
  }

  // The dash left a double space behind it.
  const auto doubled = out.find("  ");
  if (doubled != std::string::npos)
  {
    out.erase(doubled, 1);
  }
  return out;
}

/// The name to put in a verdict sentence. `They` when the rival is not named, which happens when
/// the contact is with somebody the fog has not introduced yet.
[[nodiscard]] std::string NameFor(const MatchState& _state, OwnerId _owner)
{
  if (_owner == NOBODY || _owner >= static_cast<OwnerId>(_state.players.size()))
  {
    return "They";
  }
  return _state.players[static_cast<std::size_t>(_owner)].label;
}

/// Everything this player could say to somebody else this tick.
///
/// **Composed on the client from the snapshot, exactly as the build rows are**, and for the same
/// reason: the server has no opinion about what an offer should be called, and everything needed to
/// find one is already on the wire. A lane carries whether a trade lane runs on it; a proposal is
/// sent to both parties, so this player's own offers are here to be withdrawn; a system carries its
/// owner, so a border is two systems on one lane with two owners.
///
/// Rows are built in snapshot order -- lanes then players, both already sorted by id -- so the list
/// does not reshuffle under a finger between one tick and the next.
void ComposeSignals(MatchState& _state, const Snapshot& _snapshot)
{
  /// What the panel can show without scrolling, which it cannot do. Everything composed past this
  /// is counted and not listed, the same bargain `availableBuilds` makes.
  constexpr std::size_t MAXIMUM_ROWS = 14;
  /// The one number in an offer that the player does not choose. Three ticks is the one-pager's
  /// own example, and a hold nobody can enforce is not improved by making its length adjustable.
  constexpr std::uint32_t HOLD_TICKS = 3;

  const std::int32_t viewer = _state.viewer;
  std::vector<SignalRow>& rows = _state.orders.signals;

  const auto nameOf = [&_state](std::int32_t _player) { return NameOfPlayer(_state.players, _player); };

  const auto ownerOf = [&_snapshot](SystemId _system) -> std::int32_t
  {
    const SnapshotSystem* system = _snapshot.System(_system);
    return system != nullptr && system->live && system->owner.IsValid() ? system->owner.Index() : EventRefs::NONE;
  };

  const auto nameOfSystem = [&_snapshot](SystemId _system)
  {
    const SnapshotSystem* system = _snapshot.System(_system);
    return system != nullptr ? system->name : std::string{"?"};
  };

  // ---- Offers this player made and can take back ---------------------------------------------
  //
  // First, because a retraction is time-critical in a way an opening offer is not: an offer that
  // has been sitting for three of its four ticks is about to be reported as ignored.
  for (const SnapshotProposal& proposal : _snapshot.Proposals())
  {
    if (proposal.from.Index() != viewer)
    {
      continue;
    }
    const char* what = proposal.kind == ProposalKind::OpenLane        ? "lane"
                       : proposal.kind == ProposalKind::ShareScouting ? "scouting"
                                                                      : "hold fire";
    rows.push_back(SignalRow{.kind = SignalKind::Withdraw,
                             .title = std::format("Withdraw {} - {}", what, nameOf(proposal.to.Index())),
                             .detail = std::format("Unanswered for {} more tick(s)", proposal.ticksLeft),
                             .proposal = proposal.id.Index()});
  }

  // ---- Trade lanes this player is on -----------------------------------------------------------
  for (const SnapshotLane& lane : _snapshot.Lanes())
  {
    const std::int32_t a = ownerOf(lane.a);
    const std::int32_t b = ownerOf(lane.b);
    if (!lane.tradeLane || (a != viewer && b != viewer))
    {
      continue;
    }
    rows.push_back(SignalRow{.kind = SignalKind::CancelLane,
                             .title = std::format("Close lane - {} to {}", nameOfSystem(lane.a), nameOfSystem(lane.b)),
                             .detail = "Instant, and everybody sees it",
                             .lane = lane.id.Index()});
  }

  // ---- Borders worth opening a lane on ---------------------------------------------------------
  //
  // A lane with one of this player's systems at one end and somebody else's at the other. That is
  // the same rule the Diplomat bot uses to find an offer, which is not a coincidence: it is where
  // a trade lane can exist.
  for (const SnapshotLane& lane : _snapshot.Lanes())
  {
    if (lane.tradeLane)
    {
      continue;
    }
    const std::int32_t a = ownerOf(lane.a);
    const std::int32_t b = ownerOf(lane.b);
    const bool mineThenTheirs = a == viewer && b != viewer && b != EventRefs::NONE;
    const bool theirsThenMine = b == viewer && a != viewer && a != EventRefs::NONE;
    if (!mineThenTheirs && !theirsThenMine)
    {
      continue;
    }

    const std::int32_t other = mineThenTheirs ? b : a;
    rows.push_back(SignalRow{.kind = SignalKind::OpenLane,
                             .title = std::format("Open lane - {} to {}", nameOfSystem(lane.a), nameOfSystem(lane.b)),
                             .detail = std::format("With {} - pays both sides", nameOf(other)),
                             .to = other,
                             .lane = lane.id.Index()});
  }

  // ---- Everybody this player has actually met ---------------------------------------------------
  //
  // Met means "owns a system this player can see now", which is the same thing first contact is
  // reported on. Offering to share maps with an empire nobody has found yet would be offering to
  // share a map of somewhere neither of them has been.
  std::vector<std::int32_t> met;
  for (const SnapshotSystem& system : _snapshot.Systems())
  {
    if (!system.live || !system.owner.IsValid() || system.owner.Index() == viewer)
    {
      continue;
    }
    if (std::ranges::find(met, system.owner.Index()) == met.end())
    {
      met.push_back(system.owner.Index());
    }
  }
  std::ranges::sort(met);

  for (const std::int32_t other : met)
  {
    rows.push_back(SignalRow{.kind = SignalKind::ShareScouting,
                             .title = std::format("Share scouting - {}", nameOf(other)),
                             .detail = "Their map is your map, while it stands",
                             .to = other});
  }
  for (const std::int32_t other : met)
  {
    rows.push_back(SignalRow{.kind = SignalKind::HoldFire,
                             .title = std::format("Hold fire {} ticks - {}", HOLD_TICKS, nameOf(other)),
                             .detail = "Nothing enforces it. That is the point of it",
                             .to = other,
                             .ticks = HOLD_TICKS});
  }

  _state.orders.availableSignals = static_cast<std::uint32_t>(rows.size());
  if (rows.size() > MAXIMUM_ROWS)
  {
    rows.resize(MAXIMUM_ROWS);
  }

  // ---- Conceding -------------------------------------------------------------------------------
  //
  // Last, always, and never trimmed away. It is the one thing on this list a player cannot undo
  // after it resolves, so it is also the one that must not move around: a row that changes
  // position between ticks is a row somebody double-taps by accident.
  if (!_snapshot.IsFinished())
  {
    // The row states the cost rather than implying it (ADR-067): the score goes, in any week, and
    // this line is what a player reads between the first tap and the confirming second (ADR-064).
    rows.push_back(
      SignalRow{.kind = SignalKind::Concede, .title = "Concede", .detail = "To a custodian, permanently - your score is forfeit"});
    ++_state.orders.availableSignals;
  }
}

/// Where a system with this id sits in the view's own list, or NONE.
///
/// **A system id and a position in `graph.systems` are different numbers** (ADR-057): the graph is
/// fogged, so the tenth system a player can see is not system ten. Every section below that names
/// a system names a position, and this is the one place the two are related.
[[nodiscard]] std::int32_t PositionOf(const MatchState& _state, SystemId _system)
{
  for (std::size_t index = 0; index < _state.graph.systems.size(); ++index)
  {
    if (_state.graph.systems[index].id == _system.Index())
    {
      return static_cast<std::int32_t>(index);
    }
  }
  return EventRefs::NONE;
}

/// Who is in the match, in seat order: the badge every owner colour on the screen is read from.
void ComposePlayers(MatchState& _state, const Snapshot& _snapshot)
{
  // ---- Players -----------------------------------------------------------------------------------
  for (const SnapshotStanding& standing : _snapshot.Standings())
  {
    PlayerBadge badge;
    badge.isYou = standing.player == _snapshot.Viewer();
    badge.label = badge.isYou ? "YOU" : std::format("P{}", standing.player.Index() + 1);
    badge.score = standing.score;
    badge.placement = standing.placement;
    badge.custodian = standing.status == PlayerStatus::Custodian;
    badge.custodianSince = standing.custodianSince;
    _state.players.push_back(std::move(badge));
  }
}

/// The header every screen carries: which tick, how long is left of it, and whether it is over.
void ComposeMatch(MatchState& _state, const Snapshot& _snapshot, std::int64_t _secondsToLock)
{
  // ---- The match ---------------------------------------------------------------------------------
  _state.match.id = std::format("{:04}", _snapshot.Tick());
  _state.match.tick = _snapshot.Tick();
  _state.match.secondsToLock = static_cast<double>(_secondsToLock);
  _state.match.day = 1 + _snapshot.Tick() / 4;
  _state.match.totalDays = 21;
  _state.match.endsAt = "";
  _state.match.finished = _snapshot.IsFinished();

  // A finished match is locked and stays locked. `locked` is what every control on the screen
  // already reads, so this is one assignment rather than a second disabled state to maintain.
  _state.orders.locked = _state.orders.locked || _state.match.finished;
}

/// Where this player stands, which is the top bar's half of the view model.
void ComposeStanding(MatchState& _state, const Snapshot& _snapshot)
{
  // ---- Standing ----------------------------------------------------------------------------------
  const OwnerId viewer = _state.viewer;
  if (viewer >= 0 && viewer < static_cast<OwnerId>(_state.players.size()))
  {
    const PlayerBadge& mine = _state.players[static_cast<std::size_t>(viewer)];
    _state.player.score = mine.score;
    _state.player.placement = mine.placement;
  }
  _state.player.credits = _snapshot.Credits();
  _state.player.playerCount = static_cast<std::uint32_t>(_state.players.size());

  for (const PlayerBadge& badge : _state.players)
  {
    if (badge.placement == 1)
    {
      _state.player.leader = Leader{.name = badge.label, .score = badge.score};
    }
  }
}

/// The systems and lanes this player can see, fog included (ADR-022).
///
/// **Composed first of the three that name a system**, because every one of them names a system by
/// POSITION in this list rather than by id (ADR-057), and there is no list to take a position in
/// until this has run.
void ComposeGraph(MatchState& _state, const Snapshot& _snapshot)
{
  // ---- The graph ---------------------------------------------------------------------------------
  //
  // The snapshot's positions are integers in the 800x560 design space and the view model's are
  // floats, because the next thing that happens to them is a projection (Galaxy.h).
  for (const SnapshotSystem& system : _snapshot.Systems())
  {
    SystemNode node;
    node.id = system.id.Index();
    node.name = system.name;
    node.owner = system.owner.IsValid() ? static_cast<OwnerId>(system.owner.Index()) : NOBODY;
    node.positionX = static_cast<float>(system.positionX);
    node.positionY = static_cast<float>(system.positionY);
    node.flags = FlagsOf(system, _snapshot.Tick());
    node.production = system.production;
    node.capturedAt = system.capturedAt;
    node.capturedFrom = system.capturedFrom.IsValid() ? static_cast<OwnerId>(system.capturedFrom.Index()) : NOBODY;

    // A custodian's territory is flagged on every player's map, and the stamp is the tick they
    // went into custody rather than anything about the system.
    if (node.owner != NOBODY && node.owner < static_cast<OwnerId>(_state.players.size()) &&
        _state.players[static_cast<std::size_t>(node.owner)].custodian)
    {
      node.custodianSince = _state.players[static_cast<std::size_t>(node.owner)].custodianSince;
    }

    _state.graph.systems.push_back(std::move(node));
  }

  // Lanes are re-indexed into the view's own system list, because a fogged snapshot is not the
  // whole galaxy and a lane naming absolute system ids would point past the end of it.

  for (const SnapshotLane& lane : _snapshot.Lanes())
  {
    const std::int32_t a = PositionOf(_state, lane.a);
    const std::int32_t b = PositionOf(_state, lane.b);
    if (a == EventRefs::NONE || b == EventRefs::NONE)
    {
      continue;
    }
    _state.graph.lanes.push_back(
      Lane{.id = lane.id.Index(), .a = a, .b = b, .cost = lane.costTicks, .kind = lane.tradeLane ? LaneKind::Trade : LaneKind::None});
  }
}

void ComposeFleets(MatchState& _state, const Snapshot& _snapshot)
{
  // ---- Fleets ------------------------------------------------------------------------------------
  for (const SnapshotFleet& fleet : _snapshot.Fleets())
  {
    const bool moving = fleet.ticksRemaining > 0;
    const std::int32_t from = PositionOf(_state, moving ? fleet.movingFrom : fleet.at);
    const std::int32_t to = PositionOf(_state, moving ? fleet.movingTo : fleet.at);
    if (from == EventRefs::NONE || to == EventRefs::NONE)
    {
      // Under way between two systems this player cannot see. It is public that it exists, and
      // there is nowhere on this map to draw it.
      continue;
    }

    Fleet entry;
    entry.id = fleet.id.Index();
    entry.owner = fleet.owner.IsValid() ? static_cast<OwnerId>(fleet.owner.Index()) : NOBODY;
    entry.ships = fleet.ships;
    entry.from = from;
    entry.to = to;
    entry.order = moving ? FleetStance::Move : FleetStance::Hold;
    entry.underWay = moving;

    // **How far along the lane it actually is** (ADR-055), from two numbers already on the wire:
    // the lane's cost and the ticks left. Departure sets the remaining ticks to the whole cost and
    // spends one immediately, so a fleet visible in transit has between one tick and cost-minus-one
    // left, and `(cost - left) / cost` is where it stands. A fixed midpoint was right only for the
    // two-tick lane and put a three-tick fleet in the wrong place twice.
    std::uint32_t costTicks = 0;
    for (const SnapshotLane& lane : _snapshot.Lanes())
    {
      const bool joins = (PositionOf(_state, lane.a) == from && PositionOf(_state, lane.b) == to) ||
                         (PositionOf(_state, lane.a) == to && PositionOf(_state, lane.b) == from);
      if (joins)
      {
        costTicks = lane.costTicks;
        break;
      }
    }
    entry.progress = moving && costTicks > 0 && fleet.ticksRemaining < costTicks
                       ? static_cast<float>(costTicks - fleet.ticksRemaining) / static_cast<float>(costTicks)
                       : (moving ? 0.5F : 0.0F);
    entry.eta = _snapshot.Tick() + fleet.ticksRemaining;
    entry.preview = fleet.preview;
    entry.name = std::format("FLT {}", fleet.id.Index() + 1);
    entry.status = moving ? std::format("in transit - ETA T{}", entry.eta)
                          : std::format("holding {}", _state.graph.systems[static_cast<std::size_t>(to)].name);

    _state.fleets.push_back(std::move(entry));
  }
}

void ComposeProposals(MatchState& _state, const Snapshot& _snapshot)
{
  // ---- Proposals ---------------------------------------------------------------------------------
  for (const SnapshotProposal& proposal : _snapshot.Proposals())
  {
    if (proposal.to != _snapshot.Viewer())
    {
      // An offer this player MADE. It is not in the PROPOSALS rail, which is the list of things
      // awaiting this player's answer, but it is not dropped either any more: it becomes a
      // `Withdraw` row below, which is the only thing a player can still do about it (ADR-039).
      continue;
    }

    Proposal entry;
    entry.id = proposal.id.Index();
    entry.from = NameOfPlayer(_state.players, proposal.from.Index());
    entry.type = proposal.kind == ProposalKind::OpenLane        ? ProposalType::OpenLane
                 : proposal.kind == ProposalKind::ShareScouting ? ProposalType::ShareScouting
                                                                : ProposalType::HoldForTicks;
    entry.ticksLeft = proposal.ticksLeft;
    entry.conditionalLane = proposal.lane.Index();
    entry.terms = proposal.kind == ProposalKind::OpenLane        ? "Open a trade lane - pays both sides"
                  : proposal.kind == ProposalKind::ShareScouting ? "Share scouting - their map is your map"
                                                                 : std::format("Hold for {} ticks - nothing enforces it", proposal.ticks);
    _state.proposals.push_back(std::move(entry));
  }
}

void ComposeBuilds(MatchState& _state, const Snapshot& _snapshot)
{
  // ---- Builds ------------------------------------------------------------------------------------
  //
  // Composed here rather than sent, because a build row is a thing the client offers and the server
  // has no opinion about what it should be called.
  for (const SystemNode& node : _state.graph.systems)
  {
    if (node.owner != _state.viewer)
    {
      continue;
    }
    const SnapshotSystem* source = nullptr;
    for (const SnapshotSystem& system : _snapshot.Systems())
    {
      if (system.id.Index() == node.id)
      {
        source = &system;
        break;
      }
    }
    if (source == nullptr)
    {
      continue;
    }

    // Priced from the snapshot, never from a number the client knows (ADR-053): the server owns
    // the rules and the client owns the sentence.
    //
    // A row offers THE NEXT LEVEL (ADR-069), so a system that has a level-one shipyard offers L2 at
    // L2's price and L2's build time.
    //
    // **A system that is building composes its OTHER rows too, marked unavailable** (ADR-107,
    // amending ADR-070). It used to compose the rising row and stop, which left the build sheet
    // with one row on it and nothing to say what that system will be able to take when the build
    // lands. `available` is what carries "the lock would start this" from here to the three readers
    // that have to agree about it -- the sheet, the digest's offer, and `N AVAIL` on the rail --
    // and the building that is ALREADY rising composes no second row, because the rising row is
    // that building's next level.
    const bool building = source->risingCompletesAt != 0;
    const bool yardRising = building && source->risingKind == BuildKind::Shipyard;
    const bool mineRising = building && source->risingKind == BuildKind::MiningStation;

    if (building)
    {
      const bool yard = source->risingKind == BuildKind::Shipyard;
      const std::uint32_t risingTicks = _snapshot.LevelTicks(source->risingKind, source->risingToLevel);
      const std::uint32_t risingCost = _snapshot.LevelCost(source->risingKind, source->risingToLevel);

      // **What an earlier lock already took, and what it buys when it lands** (ADR-107). The tick
      // it was ordered on is derived rather than sent: a construction completes `ticks` after the
      // lock that started it (ADR-069), so `completesAt - ticks` is the tick the credits left the
      // purse on. It is also what the sheet's progress bar is a fraction of.
      const std::uint32_t orderedAt = source->risingCompletesAt > risingTicks ? source->risingCompletesAt - risingTicks : 0;
      _state.orders.builds.push_back(
        BuildRow{.title = std::format("{} L{} - {}", yard ? "Shipyard" : "Mining station", source->risingToLevel, node.name),
                 .detail = std::format("Ordered T{} · {} credits spent · +{} a tick", orderedAt, risingCost,
                                       _snapshot.LevelYield(source->risingKind, source->risingToLevel)),
                 .building = yard ? "Shipyard" : "Mining station",
                 .system = node.id,
                 .kind = static_cast<std::uint8_t>(yard ? 0 : 1),
                 .level = source->risingToLevel,
                 .ticks = risingTicks,
                 .rising = true,
                 .completesAt = source->risingCompletesAt,
                 .available = false,
                 .cost = risingCost});
    }

    if (!yardRising && source->shipyardLevel < BUILDING_LEVELS)
    {
      const std::uint32_t level = source->shipyardLevel + 1;
      const std::uint32_t ticks = _snapshot.LevelTicks(BuildKind::Shipyard, level);
      _state.orders.builds.push_back(
        BuildRow{.title = std::format("Shipyard L{} - {}", level, node.name),
                 // `·` between two peer facts, `-` only where a thing is joined to its subject
                 // (DESIGN-GUIDELINES §Font). What the level pays and how long it takes are peers.
                 .detail = std::format("+{} ships a tick · {} tick{}", _snapshot.LevelYield(BuildKind::Shipyard, level), ticks,
                                       ticks == 1 ? "" : "s"),
                 .building = "Shipyard",
                 .system = node.id,
                 .kind = 0,
                 .level = level,
                 .ticks = ticks,
                 .available = !building,
                 .cost = _snapshot.LevelCost(BuildKind::Shipyard, level)});
    }
    if (!mineRising && source->miningStationLevel < BUILDING_LEVELS)
    {
      const std::uint32_t level = source->miningStationLevel + 1;
      const std::uint32_t ticks = _snapshot.LevelTicks(BuildKind::MiningStation, level);
      _state.orders.builds.push_back(
        BuildRow{.title = std::format("Mining station L{} - {}", level, node.name),
                 .detail = std::format("+{} credits a tick · {} tick{}", _snapshot.LevelYield(BuildKind::MiningStation, level), ticks,
                                       ticks == 1 ? "" : "s"),
                 .building = "Mining station",
                 .system = node.id,
                 .kind = 1,
                 .level = level,
                 .ticks = ticks,
                 .available = !building,
                 .cost = _snapshot.LevelCost(BuildKind::MiningStation, level)});
    }
  }
  // Counted over what can actually be STARTED, which is exactly what `available` says. A rising row
  // and the rows beside it on a system that is building are both on the list so the sheet can say
  // what is coming and what it will cost (ADR-069, ADR-107); `N AVAIL` counting either would offer
  // a player a number they cannot act on.
  _state.orders.availableBuilds = static_cast<std::uint32_t>(
    std::count_if(_state.orders.builds.begin(), _state.orders.builds.end(), [](const BuildRow& _row) { return _row.available; }));

  ComposeSignals(_state, _snapshot);
}

void ComposeDigest(MatchState& _state, const Snapshot& _snapshot, const std::vector<DigestEntry>& _digest)
{
  // ---- Digest ------------------------------------------------------------------------------------
  //
  // The digest is the order surface (ADR-034), so an event arrives carrying what can be done about
  // it. Only actions the client can actually carry out are attached: a drawn button that does
  // nothing is worse than a missing one, and the four the design asks for that are not here --
  // REBUILD LANE, PLAN ROUTE, WITHDRAW, HOLD FIRE -- are all outgoing signals, which nothing in
  // this state can express yet.
  /// Which build rows the digest has already put a button on, so no two cards offer the same one.
  std::vector<std::int32_t> offeredBuilds;

  for (const DigestEntry& entry : _digest)
  {
    DigestEvent event{.kind = ColorOf(entry.kind),
                      .title = entry.title,
                      .detail = entry.detail,
                      .refs =
                        EventRefs{.system = PositionOf(_state, entry.system), .lane = entry.lane.Index(), .fleet = entry.fleet.Index()}};
    event.actor = entry.other.IsValid() ? entry.other.Index() : NOBODY;

    // A proposal is answered on the proposal, which is where the player is reading about it.
    //
    // MATCHED BY THE PROPOSAL THE ENTRY NAMES (ADR-068), never by its sender: `other` is a player
    // and the two are unrelated numbers. An entry about an offer that is no longer open -- one
    // withdrawn, voided or already resolved -- finds nothing here and draws no buttons, which is
    // the right answer rather than a missing case.
    if (event.kind == EventKind::Proposal && entry.proposal.IsValid())
    {
      for (std::size_t index = 0; index < _state.proposals.size(); ++index)
      {
        if (_state.proposals[index].id != entry.proposal.Index())
        {
          continue;
        }

        event.actions.push_back(EventAction{
          .label = "ACCEPT", .kind = EventActionKind::AcceptProposal, .target = static_cast<std::int32_t>(index), .primary = true});
        event.actions.push_back(
          EventAction{.label = "DECLINE", .kind = EventActionKind::DeclineProposal, .target = static_cast<std::int32_t>(index)});
        break;
      }
    }

    // A contact the player is flying into gets the verdict and the fleet that earns it.
    if (event.kind == EventKind::Contact)
    {
      for (std::size_t index = 0; index < _state.fleets.size(); ++index)
      {
        const Fleet& fleet = _state.fleets[index];
        if (fleet.owner != _state.viewer || fleet.preview.empty() || fleet.to != event.refs.system)
        {
          continue;
        }

        event.actions.push_back(EventAction{.label = std::format("REDIRECT {}", fleet.name),
                                            .kind = EventActionKind::RedirectFleet,
                                            .target = static_cast<std::int32_t>(index),
                                            .primary = true});
        break;
      }
    }

    // **A build is offered on the event that would make a player want it, and never twice**
    // (ADR-057). `EventKind::Economy` is the COLLAPSED kind -- a claim, a lane and a production
    // line all wear it -- so testing it put the same button on every one of them, and a digest of
    // three economy events carried three copies of `MINING STATION DOTHAN`.
    //
    // A claimed system offers a building AT THAT SYSTEM, which is the order the event causes. A
    // production line offers whatever is still unoffered, which is what the credits are for. A
    // rising row is neither: it reports what an earlier lock already took, and carries no button.
    std::int32_t offeredRow = EventRefs::NONE;

    if (entry.kind == DigestKind::SystemClaimed && event.refs.system != EventRefs::NONE)
    {
      for (std::size_t row = 0; row < _state.orders.builds.size(); ++row)
      {
        const std::int32_t at = PositionOf(_state, SystemId{_state.orders.builds[row].system});
        if (at == event.refs.system && Offerable(_state, offeredBuilds, static_cast<std::int32_t>(row)))
        {
          offeredRow = static_cast<std::int32_t>(row);
          break;
        }
      }
    }
    else if (entry.kind == DigestKind::Economy)
    {
      for (std::size_t row = 0; row < _state.orders.builds.size(); ++row)
      {
        if (Offerable(_state, offeredBuilds, static_cast<std::int32_t>(row)))
        {
          offeredRow = static_cast<std::int32_t>(row);
          break;
        }
      }
    }

    if (offeredRow != EventRefs::NONE)
    {
      // `SHIPYARD JANDAL | 20 CR`: the price is on the button, in its own cell (ADR-053, ADR-111).
      const BuildRow& offered = _state.orders.builds[static_cast<std::size_t>(offeredRow)];
      event.actions.push_back(EventAction{.label = Shortened(offered.title),
                                          .number = std::format("{} CR", offered.cost),
                                          .kind = EventActionKind::QueueBuild,
                                          .target = offeredRow,
                                          .primary = true});
      offeredBuilds.push_back(offeredRow);
    }

    if (event.refs.system != EventRefs::NONE)
    {
      event.actions.push_back(EventAction{.label = "MAP", .kind = EventActionKind::Focus, .target = event.refs.system});
    }

    _state.digest.push_back(std::move(event));
  }

  // The verdict, from the numbers rather than from the phrase. `SnapshotFleet` carries both sides
  // of the fight now, so the client can say what it means instead of repeating `14 v 11`.
  for (std::size_t index = 0; index < _snapshot.Fleets().size(); ++index)
  {
    const SnapshotFleet& source = _snapshot.Fleets()[index];
    if (source.owner.Index() != _state.viewer || source.preview.empty() || source.previewTheirs == 0)
    {
      continue;
    }

    const std::int32_t destination = PositionOf(_state, source.movingTo);
    for (DigestEvent& event : _state.digest)
    {
      if (event.kind != EventKind::Contact || event.refs.system != destination)
      {
        continue;
      }

      const bool win = source.previewMineAfter > 0 && source.previewTheirsAfter == 0;
      const bool hold = source.previewMineAfter > 0 && source.previewTheirsAfter > 0;
      const char* outcome = win ? "YOU WIN" : (hold ? "HOLD" : "YOU LOSE");

      event.verdict = std::format("FLT{} ARRIVES T{} - {}", source.id.Index() + 1, _state.match.tick + source.ticksRemaining, outcome);
      event.verdictDetail =
        std::format("You arrive {}. {} holds {}{}. {} of theirs remain, {} of yours.", source.previewMine, NameFor(_state, event.actor),
                    source.previewTheirs, source.previewDefended ? " +def" : "", source.previewTheirsAfter, source.previewMineAfter);
      break;
    }
  }
}

void ComposeRegion(MatchState& _state, const Snapshot& _snapshot)
{
  // ---- The region ---------------------------------------------------------------------------------
  _state.region.anchor = PositionOf(_state, _snapshot.RegionAnchor());
  _state.region.opensAt = _snapshot.RegionOpensAt();
  _state.region.siteOffsets = {{-18.0F, -6.0F}, {12.0F, -14.0F}, {6.0F, 12.0F}};

  _state.totalSystems = _snapshot.TotalSystems();
  _state.unclaimedSystems = _snapshot.UnclaimedSystems();
}

} // namespace

/// One snapshot and this tick's digest, turned into everything the screens read (ADR-025).
///
/// **The wire carries records and the screens carry a view model**, and this is the whole of the
/// distance between them: the client never sees a `Snapshot` and `GameLogic` never sees a
/// `MatchState`. Composed section by section, and the graph goes first because every section after
/// it names a system by its position in that list rather than by its id (ADR-057).
MatchState ViewOf(const Snapshot& _snapshot, const std::vector<DigestEntry>& _digest, std::int64_t _secondsToLock)
{
  MatchState state;
  state.viewer = _snapshot.Viewer().Index();

  ComposePlayers(state, _snapshot);
  ComposeMatch(state, _snapshot, _secondsToLock);
  ComposeStanding(state, _snapshot);
  ComposeGraph(state, _snapshot);
  ComposeFleets(state, _snapshot);
  ComposeProposals(state, _snapshot);
  ComposeBuilds(state, _snapshot);
  ComposeDigest(state, _snapshot, _digest);
  ComposeRegion(state, _snapshot);

  return state;
}

FreshState StateFrom(const std::vector<std::uint8_t>& _snapshot, const std::vector<Neuron::Protocol::TickDigest>& _digests,
                     std::uint32_t _drawnTick, std::int64_t _secondsToLock)
{
  Neuron::ByteReader reader{_snapshot};
  const Snapshot snapshot = Snapshot::Read(reader);

  std::vector<DigestEntry> digest;
  bool digestsDecoded = true;
  for (const Neuron::Protocol::TickDigest& carried : _digests)
  {
    if (carried.tick <= _drawnTick)
    {
      continue;
    }

    Neuron::ByteReader digestReader{carried.bytes};
    for (DigestEntry& entry : Snapshot::ReadDigest(digestReader))
    {
      digest.push_back(std::move(entry));
    }
    digestsDecoded = digestsDecoded && !digestReader.Failed() && digestReader.AtEnd();
  }

  if (reader.Failed() || !reader.AtEnd() || !digestsDecoded)
  {
    return FreshState{};
  }

  FreshState fresh{.decoded = true, .state = ViewOf(snapshot, digest, _secondsToLock)};
  fresh.state.connected = true;

  // **Only the caller can know how much happened while nobody was looking**, because it is the only
  // thing that sees one state replaced by the next. A client that stayed connected gets every tick
  // as it resolves and is never behind; one that closed its lid for a night comes back to a tick
  // several later than the one it last drew, and the difference is what it missed.
  if (_drawnTick != 0 && fresh.state.match.tick > _drawnTick + 1)
  {
    fresh.state.unreadTicks = fresh.state.match.tick - _drawnTick;
    fresh.state.lastSeenTick = _drawnTick;
  }
  return fresh;
}

OrderSet OrdersOf(const MatchState& _state)
{
  OrderSet orders;
  orders.player = PlayerId{_state.viewer};

  // **A fleet already on a lane is not re-ordered** (ADR-077). It comes back from every snapshot
  // with `order` set to `Move`, so sending one order per moving fleet sent a fresh order for a
  // fleet in transit on every tap -- and `Match::Validate` refuses each of them as
  // `FleetInTransit`, which reaches the player a tick later as `Order refused`. What travels is a
  // move the player ordered this tick, which is exactly a `Move` that is not yet under way.
  for (const Fleet& fleet : _state.fleets)
  {
    if (fleet.owner != _state.viewer || fleet.order != FleetStance::Move || fleet.underWay || fleet.id == EventRefs::NONE)
    {
      continue;
    }
    const std::int32_t destination = _state.graph.systems[static_cast<std::size_t>(fleet.to)].id;
    orders.fleetOrders.push_back(FleetOrder{.fleet = FleetId{fleet.id}, .destination = SystemId{destination}});
  }

  for (const std::int32_t queued : _state.orders.queuedBuilds)
  {
    if (queued < 0 || queued >= static_cast<std::int32_t>(_state.orders.builds.size()))
    {
      continue;
    }
    const BuildRow& row = _state.orders.builds[static_cast<std::size_t>(queued)];
    if (row.system == EventRefs::NONE)
    {
      continue;
    }
    orders.builds.push_back(
      BuildOrder{.system = SystemId{row.system}, .kind = row.kind == 0 ? BuildKind::Shipyard : BuildKind::MiningStation});
  }

  // ---- Signals ---------------------------------------------------------------------------------
  //
  // Four order kinds that had no way out of this client until ADR-039. Each row knows which one it
  // is and carries the one or two fields that order needs, which is why a `SignalRow` holds a lane
  // id and a proposal id rather than a screen position: a row has to be able to BECOME an order.
  for (const std::int32_t queued : _state.orders.queuedSignals)
  {
    if (queued < 0 || queued >= static_cast<std::int32_t>(_state.orders.signals.size()))
    {
      continue;
    }
    const SignalRow& row = _state.orders.signals[static_cast<std::size_t>(queued)];

    switch (row.kind)
    {
    case SignalKind::OpenLane:
      orders.proposals.push_back(
        ProposalOrder{.to = PlayerId{row.to}, .kind = ProposalKind::OpenLane, .lane = LaneId{row.lane}, .ticks = 0});
      break;

    case SignalKind::ShareScouting:
      orders.proposals.push_back(ProposalOrder{.to = PlayerId{row.to}, .kind = ProposalKind::ShareScouting});
      break;

    case SignalKind::HoldFire:
      orders.proposals.push_back(ProposalOrder{.to = PlayerId{row.to}, .kind = ProposalKind::HoldForTicks, .ticks = row.ticks});
      break;

    case SignalKind::Withdraw:
      orders.withdrawals.push_back(WithdrawOrder{.proposal = ProposalId{row.proposal}});
      break;

    case SignalKind::CancelLane:
      orders.cancellations.push_back(CancelLaneOrder{.lane = LaneId{row.lane}});
      break;

    case SignalKind::Concede:
      orders.concede = true;
      break;

    default:
      break;
    }
  }

  // Every offer the player answered, not the last one they touched (ADR-068). The index is into
  // the view's own proposal list and the id is what the server knows it by.
  for (const ProposalAnswer& answered : _state.orders.answers)
  {
    if (answered.proposal < 0 || answered.proposal >= static_cast<std::int32_t>(_state.proposals.size()))
    {
      continue;
    }
    orders.answers.push_back(AnswerOrder{.proposal = ProposalId{_state.proposals[static_cast<std::size_t>(answered.proposal)].id},
                                         .answer = answered.accepted ? Answer::Accept : Answer::Decline});
  }

  return orders;
}

} // namespace Lockstep
