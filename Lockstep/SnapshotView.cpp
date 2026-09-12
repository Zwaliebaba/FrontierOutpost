// SnapshotView.cpp -- a snapshot rendered as a screen, and a screen read back as orders.
//
// Two directions, and they are not symmetric on purpose. Coming in, almost everything is copied and
// a few things are composed into prose the screen needs. Going out, almost nothing is sent: only
// what the player can have changed. A client that echoed the state back would be a client asserting
// things it does not own, and the server would have to decide which parts to believe.

#include "pch.h"
#include "SnapshotView.h"

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
    rows.push_back(SignalRow{.kind = SignalKind::Concede, .title = "Concede", .detail = "Hand this empire to a custodian. Permanent."});
    ++_state.orders.availableSignals;
  }
}

} // namespace

MatchState ViewOf(const Snapshot& _snapshot, const std::vector<DigestEntry>& _digest, std::int64_t _secondsToLock)
{
  MatchState state;
  state.viewer = _snapshot.Viewer().Index();

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
    state.players.push_back(std::move(badge));
  }

  // ---- The match ---------------------------------------------------------------------------------
  state.match.id = std::format("{:04}", _snapshot.Tick());
  state.match.tick = _snapshot.Tick();
  state.match.secondsToLock = static_cast<double>(_secondsToLock);
  state.match.day = 1 + _snapshot.Tick() / 4;
  state.match.totalDays = 21;
  state.match.endsAt = "";
  state.match.finished = _snapshot.IsFinished();

  // A finished match is locked and stays locked. `locked` is what every control on the screen
  // already reads, so this is one assignment rather than a second disabled state to maintain.
  state.orders.locked = state.orders.locked || state.match.finished;

  // ---- Standing ----------------------------------------------------------------------------------
  const OwnerId viewer = state.viewer;
  if (viewer >= 0 && viewer < static_cast<OwnerId>(state.players.size()))
  {
    const PlayerBadge& mine = state.players[static_cast<std::size_t>(viewer)];
    state.player.score = mine.score;
    state.player.placement = mine.placement;
  }
  state.player.playerCount = static_cast<std::uint32_t>(state.players.size());

  for (const PlayerBadge& badge : state.players)
  {
    if (badge.placement == 1)
    {
      state.player.leader = Leader{.name = badge.label, .score = badge.score};
    }
  }

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
    node.capturedAt = system.capturedAt;

    // A custodian's territory is flagged on every player's map, and the stamp is the tick they
    // went into custody rather than anything about the system.
    if (node.owner != NOBODY && node.owner < static_cast<OwnerId>(state.players.size()) &&
        state.players[static_cast<std::size_t>(node.owner)].custodian)
    {
      node.custodianSince = state.players[static_cast<std::size_t>(node.owner)].custodianSince;
    }

    state.graph.systems.push_back(std::move(node));
  }

  // Lanes are re-indexed into the view's own system list, because a fogged snapshot is not the
  // whole galaxy and a lane naming absolute system ids would point past the end of it.
  const auto positionOf = [&state](SystemId _system)
  {
    for (std::size_t index = 0; index < state.graph.systems.size(); ++index)
    {
      if (state.graph.systems[index].id == _system.Index())
      {
        return static_cast<std::int32_t>(index);
      }
    }
    return EventRefs::NONE;
  };

  for (const SnapshotLane& lane : _snapshot.Lanes())
  {
    const std::int32_t a = positionOf(lane.a);
    const std::int32_t b = positionOf(lane.b);
    if (a == EventRefs::NONE || b == EventRefs::NONE)
    {
      continue;
    }
    state.graph.lanes.push_back(
      Lane{.id = lane.id.Index(), .a = a, .b = b, .cost = lane.costTicks, .kind = lane.tradeLane ? LaneKind::Trade : LaneKind::None});
  }

  // ---- Fleets ------------------------------------------------------------------------------------
  for (const SnapshotFleet& fleet : _snapshot.Fleets())
  {
    const bool moving = fleet.ticksRemaining > 0;
    const std::int32_t from = positionOf(moving ? fleet.movingFrom : fleet.at);
    const std::int32_t to = positionOf(moving ? fleet.movingTo : fleet.at);
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
    entry.progress = moving ? 0.5F : 0.0F;
    entry.eta = _snapshot.Tick() + fleet.ticksRemaining;
    entry.preview = fleet.preview;
    entry.name = std::format("FLT {}", fleet.id.Index() + 1);
    entry.status = moving ? std::format("in transit - ETA T{}", entry.eta)
                          : std::format("holding {}", state.graph.systems[static_cast<std::size_t>(to)].name);

    state.fleets.push_back(std::move(entry));
  }

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
    entry.from = NameOfPlayer(state.players, proposal.from.Index());
    entry.type = proposal.kind == ProposalKind::OpenLane        ? ProposalType::OpenLane
                 : proposal.kind == ProposalKind::ShareScouting ? ProposalType::ShareScouting
                                                                : ProposalType::HoldForTicks;
    entry.ticksLeft = proposal.ticksLeft;
    entry.conditionalLane = proposal.lane.Index();
    entry.terms = proposal.kind == ProposalKind::OpenLane        ? "Open a trade lane - pays both sides"
                  : proposal.kind == ProposalKind::ShareScouting ? "Share scouting - their map is your map"
                                                                 : std::format("Hold for {} ticks - nothing enforces it", proposal.ticks);
    state.proposals.push_back(std::move(entry));
  }

  // ---- Builds ------------------------------------------------------------------------------------
  //
  // Composed here rather than sent, because a build row is a thing the client offers and the server
  // has no opinion about what it should be called.
  for (const SystemNode& node : state.graph.systems)
  {
    if (node.owner != state.viewer)
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

    if (!source->hasShipyard)
    {
      state.orders.builds.push_back(BuildRow{
        .title = std::format("Shipyard - {}", node.name), .detail = "Reinforces the fleet standing on it", .system = node.id, .kind = 0});
    }
    if (!source->hasMiningStation)
    {
      state.orders.builds.push_back(BuildRow{
        .title = std::format("Mining station - {}", node.name), .detail = "More credits every tick", .system = node.id, .kind = 1});
    }
  }
  state.orders.availableBuilds = static_cast<std::uint32_t>(state.orders.builds.size());

  ComposeSignals(state, _snapshot);

  // ---- Digest ------------------------------------------------------------------------------------
  //
  // The digest is the order surface (ADR-034), so an event arrives carrying what can be done about
  // it. Only actions the client can actually carry out are attached: a drawn button that does
  // nothing is worse than a missing one, and the four the design asks for that are not here --
  // REBUILD LANE, PLAN ROUTE, WITHDRAW, HOLD FIRE -- are all outgoing signals, which nothing in
  // this state can express yet.
  for (const DigestEntry& entry : _digest)
  {
    DigestEvent event{.kind = ColorOf(entry.kind),
                      .title = entry.title,
                      .detail = entry.detail,
                      .refs = EventRefs{.system = positionOf(entry.system), .lane = entry.lane.Index(), .fleet = entry.fleet.Index()}};
    event.actor = entry.other.IsValid() ? entry.other.Index() : NOBODY;

    // A proposal is answered on the proposal, which is where the player is reading about it.
    if (event.kind == EventKind::Proposal)
    {
      for (std::size_t index = 0; index < state.proposals.size(); ++index)
      {
        if (state.proposals[index].id == entry.other.Index() || state.proposals.size() == 1)
        {
          event.actions.push_back(EventAction{
            .label = "ACCEPT", .kind = EventActionKind::AcceptProposal, .target = static_cast<std::int32_t>(index), .primary = true});
          event.actions.push_back(
            EventAction{.label = "DECLINE", .kind = EventActionKind::DeclineProposal, .target = static_cast<std::int32_t>(index)});
          break;
        }
      }
    }

    // A contact the player is flying into gets the verdict and the fleet that earns it.
    if (event.kind == EventKind::Contact)
    {
      for (std::size_t index = 0; index < state.fleets.size(); ++index)
      {
        const Fleet& fleet = state.fleets[index];
        if (fleet.owner != state.viewer || fleet.preview.empty() || fleet.to != event.refs.system)
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

    if (event.kind == EventKind::Economy && !state.orders.builds.empty())
    {
      event.actions.push_back(EventAction{
        .label = Shortened(state.orders.builds.front().title), .kind = EventActionKind::QueueBuild, .target = 0, .primary = true});
    }

    if (event.refs.system != EventRefs::NONE)
    {
      event.actions.push_back(EventAction{.label = "MAP", .kind = EventActionKind::Focus, .target = event.refs.system});
    }

    state.digest.push_back(std::move(event));
  }

  // The verdict, from the numbers rather than from the phrase. `SnapshotFleet` carries both sides
  // of the fight now, so the client can say what it means instead of repeating `14 v 11`.
  for (std::size_t index = 0; index < _snapshot.Fleets().size(); ++index)
  {
    const SnapshotFleet& source = _snapshot.Fleets()[index];
    if (source.owner.Index() != state.viewer || source.preview.empty() || source.previewTheirs == 0)
    {
      continue;
    }

    const std::int32_t destination = positionOf(source.movingTo);
    for (DigestEvent& event : state.digest)
    {
      if (event.kind != EventKind::Contact || event.refs.system != destination)
      {
        continue;
      }

      const bool win = source.previewMineAfter > 0 && source.previewTheirsAfter == 0;
      const bool hold = source.previewMineAfter > 0 && source.previewTheirsAfter > 0;
      const char* outcome = win ? "YOU WIN" : (hold ? "HOLD" : "YOU LOSE");

      event.verdict = std::format("FLT{} ARRIVES T{} - {}", source.id.Index() + 1, state.match.tick + source.ticksRemaining, outcome);
      event.verdictDetail =
        std::format("You arrive {}. {} holds {}{}. {} of theirs remain, {} of yours.", source.previewMine, NameFor(state, event.actor),
                    source.previewTheirs, source.previewDefended ? " +def" : "", source.previewTheirsAfter, source.previewMineAfter);
      break;
    }
  }

  // ---- The region ---------------------------------------------------------------------------------
  state.region.anchor = positionOf(_snapshot.RegionAnchor());
  state.region.opensAt = _snapshot.RegionOpensAt();
  state.region.siteOffsets = {{-18.0F, -6.0F}, {12.0F, -14.0F}, {6.0F, 12.0F}};

  state.totalSystems = _snapshot.TotalSystems();
  state.unclaimedSystems = _snapshot.UnclaimedSystems();
  return state;
}

OrderSet OrdersOf(const MatchState& _state)
{
  OrderSet orders;
  orders.player = PlayerId{_state.viewer};

  for (const Fleet& fleet : _state.fleets)
  {
    if (fleet.owner != _state.viewer || fleet.order != FleetStance::Move || fleet.id == EventRefs::NONE)
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

  const std::int32_t answered = _state.orders.answeredProposal;
  if (answered >= 0 && answered < static_cast<std::int32_t>(_state.proposals.size()))
  {
    orders.answers.push_back(AnswerOrder{.proposal = ProposalId{_state.proposals[static_cast<std::size_t>(answered)].id},
                                         .answer = _state.orders.acceptedProposal ? Answer::Accept : Answer::Decline});
  }

  return orders;
}

} // namespace Lockstep
