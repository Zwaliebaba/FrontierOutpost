// TickResolver.cpp -- the six phases, in the order the one-pager fixes them.
//
// Read TickResolver.h first: every phase takes a `const Match&` and returns the next one, and that
// is not a style choice, it is the no-read rule made structural.
//
// TWO THINGS IN HERE ARE LOAD-BEARING AND EASY TO UNDO.
//
// The first is that every loop that reaches the result runs over an index, in ascending order --
// players, fleets, systems, lanes. Never a set, never a map, never "whichever we found first"
// (ADR-018). Two machines resolving the same tick have to visit the same things in the same order
// or they disagree about a tie.
//
// The second is that movement is phase 3 and combat is phase 4. That ordering is the one-pager's
// central consequence: a fleet ordered out is GONE before the fight it was ordered out of, so a
// hostile arriving the same tick finds an empty system. It is testable now, with combat still a
// no-op, and there is a test that does exactly that.

#include "pch.h"
#include "TickResolver.h"

namespace Frontier
{

namespace
{

/// Appends a phase record and hands back somewhere to write lines.
PhaseRecord& OpenPhase(TickLog& _log, Phase _phase)
{
  _log.phases.push_back(PhaseRecord{.phase = _phase, .lines = {}});
  return _log.phases.back();
}

void Tell(TickLog& _log, PlayerId _player, DigestEntry _entry)
{
  if (!_player.IsValid() || _player.AsSize() >= _log.digests.size())
  {
    return;
  }
  _log.digests[_player.AsSize()].push_back(std::move(_entry));
}

[[nodiscard]] std::string NameOf(const Match& _match, SystemId _system)
{
  if (!_system.IsValid())
  {
    return "somewhere";
  }
  return _match.GalaxyGraph().SystemAt(_system).name;
}

/// "Player 3". There are no player names in `GameLogic` -- an account name is the server's
/// business (4X-02) and the simulation must not depend on one to phrase a sentence.
[[nodiscard]] std::string NameOf(PlayerId _player)
{
  return std::format("Player {}", _player.Index() + 1);
}

/// Every living fleet parked at a system, in fleet order.
[[nodiscard]] std::vector<FleetId> FleetsAt(const Match& _match, SystemId _system)
{
  std::vector<FleetId> present;
  for (std::size_t index = 0; index < _match.Fleets().size(); ++index)
  {
    const MatchFleet& fleet = _match.Fleets()[index];
    if (!fleet.destroyed && !fleet.InTransit() && fleet.at == _system)
    {
      present.emplace_back(static_cast<std::int32_t>(index));
    }
  }
  return present;
}

/// The distinct owners with a fleet parked here, in player order.
[[nodiscard]] std::vector<PlayerId> OwnersPresent(const Match& _match, SystemId _system)
{
  std::vector<PlayerId> owners;
  for (const FleetId fleet : FleetsAt(_match, _system))
  {
    const PlayerId owner = _match.FleetAt(fleet).owner;
    if (std::find(owners.begin(), owners.end(), owner) == owners.end())
    {
      owners.push_back(owner);
    }
  }
  std::sort(owners.begin(), owners.end());
  return owners;
}

/// The lane joining two systems, or an invalid id.
[[nodiscard]] LaneId LaneBetween(const Galaxy& _galaxy, SystemId _from, SystemId _to)
{
  for (const LaneId lane : _galaxy.LanesAt(_from))
  {
    if (_galaxy.OtherEnd(lane, _from) == _to)
    {
      return lane;
    }
  }
  return LaneId{};
}

/// Opens a trade lane, charging the proposer, or voids it and says why to both sides.
///
/// Shared by an accepted `OpenLane` and by a conditional lane riding on any other accepted offer,
/// so the two cannot drift apart -- including the part that is easy to forget, which is that the
/// proposer may have spent the money while the offer sat on the table. "Nobody is ever shown a
/// dead offer as acceptable" cuts both ways: the answer was honest when it was given, and the
/// refusal has to reach both digests rather than only the payer's.
[[nodiscard]] bool OpenTradeLane(Match& _next, TickLog& _log, PhaseRecord& _record, LaneId _lane, PlayerId _from, PlayerId _to,
                                 std::uint32_t _tick, PlayerId _answeredBy)
{
  if (_next.FindTradeLane(_lane) != nullptr)
  {
    return false;
  }

  PlayerState& proposer = _next.MutablePlayers()[_from.AsSize()];
  if (proposer.credits < _next.Rules().tradeLaneCost)
  {
    _record.lines.push_back(std::format("{} could no longer pay for the lane", NameOf(_from)));
    Tell(_log, _from,
         DigestEntry{.kind = DigestKind::ProposalVoided,
                     .severity = Severity::PROPOSAL_RESOLVED,
                     .title = "Lane voided",
                     .detail = "You could no longer pay for it",
                     .lane = _lane,
                     .other = _answeredBy});
    Tell(_log, _to,
         DigestEntry{.kind = DigestKind::ProposalVoided,
                     .severity = Severity::PROPOSAL_RESOLVED,
                     .title = "Lane voided",
                     .detail = std::format("{} could no longer pay for it", NameOf(_from)),
                     .lane = _lane,
                     .other = _from});
    return false;
  }

  proposer.credits -= _next.Rules().tradeLaneCost;
  _next.MutableTradeLanes().push_back(ActiveTradeLane{.lane = _lane, .a = _from, .b = _to, .openedAt = _tick});

  _record.lines.push_back(std::format("trade lane opened between {} and {}", NameOf(_from), NameOf(_to)));
  for (const PlayerId side : {_from, _to})
  {
    Tell(_log, side,
         DigestEntry{.kind = DigestKind::LaneOpened,
                     .severity = Severity::LANE_CHANGED,
                     .title = "Trade lane open",
                     .detail = "It pays from this tick",
                     .lane = _lane,
                     .other = side == _from ? _to : _from});
  }
  return true;
}

/// Whether a trade lane still joins the two empires that agreed it.
[[nodiscard]] bool TradeLaneStillValid(const Match& _match, const ActiveTradeLane& _lane)
{
  const GalaxyLane& edge = _match.GalaxyGraph().LaneAt(_lane.lane);
  const PlayerId first = _match.SystemAt(edge.a).owner;
  const PlayerId second = _match.SystemAt(edge.b).owner;
  return (first == _lane.a && second == _lane.b) || (first == _lane.b && second == _lane.a);
}

} // namespace

Match TickResolver::Resolve(const Match& _before, std::span<const OrderSet> _orders, TickLog& _outLog)
{
  _outLog = TickLog{};
  _outLog.tick = _before.Tick();
  _outLog.digests.assign(_before.Players().size(), {});

  Match state = Lock(_before, _orders, _outLog);
  state = Produce(state, _outLog);
  state = Move(state, _outLog);
  state = Fight(state, _outLog);
  state = Claim(state, _outLog);
  WriteDigest(state, _outLog);

  // The tick advances last, so that everything above read one consistent number and the digest is
  // stamped with the tick it describes.
  state.SetTick(_before.Tick() + 1);
  return state;
}

// ---- Phase 1 -- lock ----------------------------------------------------------------------------
//
// Order sets arrive in whatever order the transport delivered them and are applied in PLAYER
// order, because two players spending their last credits on the same tick must resolve the same
// way on every machine.

Match TickResolver::Lock(const Match& _in, std::span<const OrderSet> _orders, TickLog& _log)
{
  Match next = _in;
  PhaseRecord& record = OpenPhase(_log, Phase::Lock);

  // One set per player, first submission wins. A retried submission is a network event, not a
  // second turn, and doubling a build because a packet arrived twice would be the worst kind of
  // bug to reproduce.
  std::vector<const OrderSet*> byPlayer(_in.Players().size(), nullptr);
  for (const OrderSet& set : _orders)
  {
    if (!_in.HasPlayer(set.player))
    {
      record.lines.push_back(std::format("refused an order set from a player not in this match"));
      continue;
    }
    if (byPlayer[set.player.AsSize()] == nullptr)
    {
      byPlayer[set.player.AsSize()] = &set;
    }
    else
    {
      record.lines.push_back(std::format("{} submitted twice; the second set was discarded", NameOf(set.player)));
    }
  }

  for (std::size_t index = 0; index < byPlayer.size(); ++index)
  {
    const OrderSet* set = byPlayer[index];
    if (set == nullptr)
    {
      continue;
    }
    const PlayerId player{static_cast<std::int32_t>(index)};

    // Validated against the TICK-START state, which is what the player was looking at when they
    // decided. Validating against a state other players have already changed would refuse an
    // order that was legal when it was given.
    const std::vector<RejectedOrder> rejected = _in.Validate(*set);
    for (const RejectedOrder& refusal : rejected)
    {
      const std::string reason = Describe(refusal.reason);
      record.lines.push_back(std::format("{}: order {} refused -- {}", NameOf(player), refusal.index, reason));
      Tell(_log, player,
           DigestEntry{.kind = DigestKind::OrderRejected, .severity = Severity::ORDER_REFUSED, .title = "Order refused", .detail = reason});
    }

    const auto wasRejected = [&rejected](std::size_t _at)
    {
      return std::any_of(rejected.begin(), rejected.end(),
                         [_at](const RejectedOrder& _refusal) { return _refusal.index == static_cast<std::int32_t>(_at); });
    };

    if (!rejected.empty() && rejected.front().reason == OrderRejection::AlreadyConceded)
    {
      continue;
    }

    next.MutablePlayers()[index].lastActiveTick = _in.Tick();

    if (set->concede)
    {
      next.MutablePlayers()[index].conceded = true;
      record.lines.push_back(std::format("{} conceded", NameOf(player)));
    }

    // Fleet orders become an intent the movement phase consumes.
    for (std::size_t order = 0; order < set->fleetOrders.size(); ++order)
    {
      if (wasRejected(order))
      {
        continue;
      }
      const FleetOrder& fleetOrder = set->fleetOrders[order];
      next.MutableFleets()[fleetOrder.fleet.AsSize()].orderedTo = fleetOrder.destination;
    }

    // Builds complete at the lock and are paid for at the lock.
    for (std::size_t order = 0; order < set->builds.size(); ++order)
    {
      if (wasRejected(order))
      {
        continue;
      }
      const BuildOrder& build = set->builds[order];
      SystemState& system = next.MutableSystems()[build.system.AsSize()];
      const std::uint32_t cost = build.kind == BuildKind::Shipyard ? _in.Rules().shipyardCost : _in.Rules().miningStationCost;

      if (build.kind == BuildKind::Shipyard)
      {
        system.hasShipyard = true;
      }
      else
      {
        system.hasMiningStation = true;
      }
      next.MutablePlayers()[index].credits -= cost;
      record.lines.push_back(std::format("{} built at {}", NameOf(player), NameOf(_in, build.system)));
    }

    // Proposals go on the table. Nothing is charged yet -- the lane is paid for at the lock the
    // partner accepts it, which is also the lock it starts paying.
    for (std::size_t order = 0; order < set->proposals.size(); ++order)
    {
      if (wasRejected(order))
      {
        continue;
      }
      const ProposalOrder& proposed = set->proposals[order];

      OpenProposal open;
      open.id = next.TakeNextProposalId();
      open.from = player;
      open.to = proposed.to;
      open.kind = proposed.kind;
      open.lane = proposed.lane;
      open.conditionalLane = proposed.conditionalLane;
      open.ticks = proposed.ticks;
      open.openedAt = _in.Tick();
      next.MutableProposals().push_back(open);

      record.lines.push_back(std::format("{} proposed to {}", NameOf(player), NameOf(proposed.to)));
      Tell(_log, proposed.to,
           DigestEntry{.kind = DigestKind::ProposalReceived,
                       .severity = Severity::PROPOSAL_ARRIVED,
                       .title = std::format("Proposal from {}", NameOf(player)),
                       .detail = "Answer at the next lock, or it is reported as ignored",
                       .lane = proposed.lane,
                       .other = player});
    }
  }

  // Answers and withdrawals are applied after every proposal in this tick exists, so that an offer
  // made and answered in the same tick behaves like any other -- and in player order, so two
  // answers to the same offer resolve the same way everywhere.
  for (std::size_t index = 0; index < byPlayer.size(); ++index)
  {
    const OrderSet* set = byPlayer[index];
    if (set == nullptr)
    {
      continue;
    }
    const PlayerId player{static_cast<std::int32_t>(index)};

    for (const AnswerOrder& answer : set->answers)
    {
      const OpenProposal* open = next.FindProposal(answer.proposal);
      if (open == nullptr || open->to != player)
      {
        continue;
      }
      const OpenProposal decided = *open;

      if (answer.answer == Answer::Accept)
      {
        // "A lane accepted at a lock opens in that same phase 1 and pays from that tick's
        // production" -- which is why this happens here, in phase 1, and not in a later phase that
        // would miss phase 2 by one tick.
        if (decided.kind == ProposalKind::OpenLane)
        {
          (void)OpenTradeLane(next, _log, record, decided.lane, decided.from, decided.to, _in.Tick(), player);
        }
        else
        {
          // The other two kinds are recorded and NOT enforced. That is the design, not an omission:
          // "no enforced treaties", and the trade lane is called the one consensual mechanic
          // because it is the only one with teeth.
          const AgreementKind kind = decided.kind == ProposalKind::ShareScouting ? AgreementKind::ShareScouting : AgreementKind::HoldFire;

          next.MutableAgreements().push_back(Agreement{.kind = kind,
                                                       .a = decided.from,
                                                       .b = decided.to,
                                                       .openedAt = _in.Tick(),
                                                       .expiresAt = kind == AgreementKind::HoldFire ? _in.Tick() + decided.ticks : 0});

          record.lines.push_back(std::format("{} and {} agreed", NameOf(decided.from), NameOf(decided.to)));
          for (const PlayerId side : {decided.from, decided.to})
          {
            Tell(_log, side,
                 DigestEntry{.kind = DigestKind::AgreementOpened,
                             .severity = Severity::AGREEMENT_MADE,
                             .title = kind == AgreementKind::ShareScouting ? "Scouting shared" : "Hold agreed",
                             .detail = kind == AgreementKind::ShareScouting
                                         ? "Their map is your map"
                                         : std::format("For {} ticks, and nothing enforces it", decided.ticks),
                             .other = side == decided.from ? decided.to : decided.from});
          }
        }

        // "It can carry a conditional order -- if accepted, open lane -- so the effect lands
        // without a second round trip."
        if (decided.conditionalLane.IsValid() && decided.conditionalLane != decided.lane)
        {
          (void)OpenTradeLane(next, _log, record, decided.conditionalLane, decided.from, decided.to, _in.Tick(), player);
        }
      }

      Tell(_log, decided.from,
           DigestEntry{.kind = DigestKind::ProposalAnswered,
                       .severity = Severity::PROPOSAL_RESOLVED,
                       .title = answer.answer == Answer::Accept ? "Proposal accepted" : "Proposal declined",
                       .detail = std::format("{} answered", NameOf(player)),
                       .lane = decided.lane,
                       .other = player});

      std::vector<OpenProposal>& answered = next.MutableProposals();
      answered.erase(std::remove_if(answered.begin(), answered.end(),
                                    [&decided](const OpenProposal& _candidate) { return _candidate.id == decided.id; }),
                     answered.end());
    }

    for (const WithdrawOrder& withdraw : set->withdrawals)
    {
      const OpenProposal* open = next.FindProposal(withdraw.proposal);
      if (open == nullptr || open->from != player)
      {
        continue;
      }
      const OpenProposal pulled = *open;

      Tell(_log, pulled.to,
           DigestEntry{.kind = DigestKind::ProposalWithdrawn,
                       .severity = Severity::PROPOSAL_RESOLVED,
                       .title = std::format("{} withdrew a proposal", NameOf(player)),
                       .detail = "It is no longer on the table",
                       .lane = pulled.lane,
                       .other = player});

      std::vector<OpenProposal>& list = next.MutableProposals();
      list.erase(std::remove_if(list.begin(), list.end(), [&pulled](const OpenProposal& _candidate) { return _candidate.id == pulled.id; }),
                 list.end());
    }

    // "Either can cancel it at any tick. Lanes are public; cancelling one is a tell." The digest
    // has to say CANCELED BY PARTNER, distinct from a lane that fell with a system in phase 5 --
    // the one-pager is explicit that "the tell only works if the reader knows which".
    for (const CancelLaneOrder& cancel : set->cancellations)
    {
      const ActiveTradeLane* found = next.FindTradeLane(cancel.lane);
      if (found == nullptr || (found->a != player && found->b != player))
      {
        continue;
      }
      const ActiveTradeLane closed = *found;
      const PlayerId partner = closed.a == player ? closed.b : closed.a;

      record.lines.push_back(std::format("{} canceled a trade lane with {}", NameOf(player), NameOf(partner)));
      Tell(_log, partner,
           DigestEntry{.kind = DigestKind::LaneCanceled,
                       .severity = Severity::LANE_CHANGED,
                       .title = "Trade lane canceled",
                       .detail = std::format("Canceled by partner -- {} closed it", NameOf(player)),
                       .lane = closed.lane,
                       .other = player});
      Tell(_log, player,
           DigestEntry{.kind = DigestKind::LaneCanceled,
                       .severity = Severity::LANE_CHANGED,
                       .title = "Trade lane canceled",
                       .detail = std::format("You closed it with {}", NameOf(partner)),
                       .lane = closed.lane,
                       .other = partner});

      std::vector<ActiveTradeLane>& lanes = next.MutableTradeLanes();
      lanes.erase(
        std::remove_if(lanes.begin(), lanes.end(), [&closed](const ActiveTradeLane& _candidate) { return _candidate.lane == closed.lane; }),
        lanes.end());
    }
  }

  // Agreements that have run their term. A hold-fire that lapses is not news -- nothing was
  // enforcing it -- so it leaves quietly rather than as an event in eleven digests.
  {
    std::vector<Agreement> standing;
    for (const Agreement& agreement : next.Agreements())
    {
      if (agreement.expiresAt == 0 || _in.Tick() < agreement.expiresAt)
      {
        standing.push_back(agreement);
      }
    }
    next.MutableAgreements() = standing;
  }

  // Re-validation. The one-pager: every open proposal is re-checked at every lock, and one that no
  // longer makes sense is voided with a reason that reaches BOTH digests. An offer that quietly
  // stopped working is exactly the dead offer the design forbids.
  {
    std::vector<OpenProposal> surviving;
    for (const OpenProposal& proposal : next.Proposals())
    {
      const bool expired = _in.Tick() >= proposal.openedAt + _in.Rules().proposalWindowTicks;

      bool stillMakesSense = true;
      if (proposal.kind == ProposalKind::OpenLane && proposal.lane.IsValid())
      {
        const GalaxyLane& edge = next.GalaxyGraph().LaneAt(proposal.lane);
        const PlayerId first = next.SystemAt(edge.a).owner;
        const PlayerId second = next.SystemAt(edge.b).owner;
        stillMakesSense = (first == proposal.from && second == proposal.to) || (second == proposal.from && first == proposal.to);
      }
      if (next.PlayerAt(proposal.from).conceded || next.PlayerAt(proposal.to).conceded)
      {
        stillMakesSense = false;
      }

      if (!stillMakesSense)
      {
        record.lines.push_back("a proposal was voided");
        for (const PlayerId side : {proposal.from, proposal.to})
        {
          Tell(_log, side,
               DigestEntry{.kind = DigestKind::ProposalVoided,
                           .severity = Severity::PROPOSAL_RESOLVED,
                           .title = "Proposal voided",
                           .detail = "The lane no longer joins your two empires",
                           .lane = proposal.lane,
                           .other = side == proposal.from ? proposal.to : proposal.from});
        }
        continue;
      }

      if (expired)
      {
        record.lines.push_back("a proposal went unanswered");
        Tell(_log, proposal.from,
             DigestEntry{.kind = DigestKind::ProposalIgnored,
                         .severity = Severity::PROPOSAL_RESOLVED,
                         .title = std::format("{} ignored your proposal", NameOf(proposal.to)),
                         .detail = std::format("No answer in {} ticks", _in.Rules().proposalWindowTicks),
                         .lane = proposal.lane,
                         .other = proposal.to});
        continue;
      }

      surviving.push_back(proposal);
    }
    next.MutableProposals() = surviving;
  }

  return next;
}

// ---- Phase 2 -- production and research -----------------------------------------------------------
//
// Research is a counter with no effect in Stage A and is not here at all: a field nothing reads is
// a number that looks tuned and is not (see the note on MatchRules in the plan's step 1).

Match TickResolver::Produce(const Match& _in, TickLog& _log)
{
  Match next = _in;
  PhaseRecord& record = OpenPhase(_log, Phase::Production);

  std::vector<std::uint32_t> earned(_in.Players().size(), 0);

  for (std::size_t index = 0; index < _in.Systems().size(); ++index)
  {
    const SystemId system{static_cast<std::int32_t>(index)};
    const SystemState& state = _in.SystemAt(system);
    if (!state.owner.IsValid())
    {
      continue;
    }

    std::uint32_t produced = _in.Rules().creditsPerSystem;
    if (_in.GalaxyGraph().SystemAt(system).kind == SystemKind::Capital)
    {
      produced += _in.Rules().capitalCreditsBonus;
    }
    if (state.hasMiningStation)
    {
      produced += _in.Rules().miningStationCredits;
    }
    earned[state.owner.AsSize()] += produced;
  }

  // Lane income. An internal lane is one whose two ends are held by the same player; it pays that
  // player once. A trade lane pays BOTH of its owners, and more -- which is the whole incentive
  // (MatchRules::tradeLaneIncome).
  for (std::size_t index = 0; index < _in.GalaxyGraph().Lanes().size(); ++index)
  {
    const GalaxyLane& lane = _in.GalaxyGraph().Lanes()[index];
    const PlayerId first = _in.SystemAt(lane.a).owner;
    const PlayerId second = _in.SystemAt(lane.b).owner;
    if (first.IsValid() && first == second)
    {
      earned[first.AsSize()] += _in.Rules().internalLaneIncome;
    }
  }

  for (const ActiveTradeLane& lane : _in.TradeLanes())
  {
    earned[lane.a.AsSize()] += _in.Rules().tradeLaneIncome;
    earned[lane.b.AsSize()] += _in.Rules().tradeLaneIncome;
  }

  for (std::size_t index = 0; index < earned.size(); ++index)
  {
    next.MutablePlayers()[index].credits += earned[index];
    Tell(_log, PlayerId{static_cast<std::int32_t>(index)},
         DigestEntry{.kind = DigestKind::Economy,
                     .severity = Severity::ECONOMY,
                     .title = std::format("Production +{}", earned[index]),
                     .detail = std::format("{} credits in hand", next.Players()[index].credits)});
  }

  // Shipyards. A yard reinforces the fleet standing on it; with no fleet of the owner's there, a
  // new one appears. A yard on a system a rival is standing on is IDLE -- that is what makes a
  // siege bite before it captures anything.
  for (std::size_t index = 0; index < _in.Systems().size(); ++index)
  {
    const SystemId system{static_cast<std::int32_t>(index)};
    const SystemState& state = _in.SystemAt(system);
    if (!state.hasShipyard || !state.owner.IsValid())
    {
      continue;
    }

    const std::vector<PlayerId> present = OwnersPresent(_in, system);
    const bool contested = std::any_of(present.begin(), present.end(), [&state](PlayerId _who) { return _who != state.owner; });
    if (contested)
    {
      record.lines.push_back(std::format("shipyard at {} is idle", NameOf(_in, system)));
      Tell(_log, state.owner,
           DigestEntry{.kind = DigestKind::Economy,
                       .severity = Severity::ECONOMY,
                       .title = std::format("Shipyard idle at {}", NameOf(_in, system)),
                       .detail = "A rival fleet is in the system",
                       .system = system});
      continue;
    }

    FleetId reinforced;
    for (const FleetId candidate : FleetsAt(_in, system))
    {
      if (_in.FleetAt(candidate).owner == state.owner)
      {
        reinforced = candidate;
        break;
      }
    }

    if (reinforced.IsValid())
    {
      next.MutableFleets()[reinforced.AsSize()].ships += _in.Rules().shipsPerShipyard;
    }
    else
    {
      MatchFleet built;
      built.owner = state.owner;
      built.ships = _in.Rules().shipsPerShipyard;
      built.at = system;
      (void)next.AddFleet(built);
    }
    record.lines.push_back(std::format("shipyard at {} produced {}", NameOf(_in, system), _in.Rules().shipsPerShipyard));
  }

  return next;
}

// ---- Phase 3 -- movement -------------------------------------------------------------------------
//
// LEAVING BEATS ARRIVING, and it beats it here rather than by a rule in the combat phase. A fleet
// with an order to go departs, unconditionally, before anything looks at who is standing where.

Match TickResolver::Move(const Match& _in, TickLog& _log)
{
  Match next = _in;
  PhaseRecord& record = OpenPhase(_log, Phase::Movement);

  for (std::size_t index = 0; index < _in.Fleets().size(); ++index)
  {
    const MatchFleet& before = _in.Fleets()[index];
    if (before.destroyed)
    {
      continue;
    }
    MatchFleet& fleet = next.MutableFleets()[index];

    // Last tick's movement is not this tick's. Combat reads both of these and would read a stale
    // answer if they carried over: a fleet that arrived three ticks ago is an incumbent.
    fleet.arrivedThisTick = false;
    fleet.departedFrom = SystemId{};

    // Departure: an order to somewhere other than where it stands.
    if (!before.InTransit() && before.orderedTo.IsValid() && before.orderedTo != before.at)
    {
      const LaneId lane = LaneBetween(_in.GalaxyGraph(), before.at, before.orderedTo);
      if (!lane.IsValid())
      {
        // Validation refused this at the lock, so reaching here means the graph and the orders
        // disagree -- a defect rather than a move.
        Neuron::Fatal("A fleet was ordered along a lane that does not exist.");
      }

      fleet.movingFrom = before.at;
      fleet.movingTo = before.orderedTo;
      fleet.departedFrom = before.at;
      fleet.at = SystemId{};
      fleet.ticksRemaining = _in.GalaxyGraph().LaneAt(lane).costTicks;
      fleet.orderedTo = SystemId{};

      record.lines.push_back(std::format("fleet {} left {} for {}", index, NameOf(_in, before.at), NameOf(_in, before.orderedTo)));
    }
    else if (!before.InTransit())
    {
      // Holding. An order to stay is consumed like any other, so it does not carry into next tick.
      fleet.orderedTo = SystemId{};
      continue;
    }

    // One tick of the lane, whether it started this tick or three ticks ago.
    if (fleet.ticksRemaining > 0)
    {
      fleet.ticksRemaining -= 1;
    }

    if (fleet.ticksRemaining == 0 && fleet.movingTo.IsValid())
    {
      fleet.at = fleet.movingTo;
      fleet.arrivedThisTick = true;
      record.lines.push_back(std::format("fleet {} arrived at {}", index, NameOf(_in, fleet.movingTo)));
      fleet.movingFrom = SystemId{};
      fleet.movingTo = SystemId{};
    }
  }

  return next;
}

// ---- Phase 4 -- combat ---------------------------------------------------------------------------

Match TickResolver::Fight(const Match& _in, TickLog& _log)
{
  Match next = _in;
  PhaseRecord& record = OpenPhase(_log, Phase::Combat);

  // ---- 4a, the rear-guard ----------------------------------------------------------------------
  //
  // "Fleets that departed a system this tick while a hostile arrived there take one free round from
  // the arrivals, computed from the arrivals' end-of-movement strength." Off by default, because
  // the one-pager keeps it off "until Phase 0 shows dancing dominates".
  //
  // It runs first and 4b reads what it leaves, which is why the phases are numbered rather than
  // merged: a fleet weakened on its way out is weaker wherever it landed.
  if (_in.Rules().rearGuardEnabled)
  {
    for (std::size_t index = 0; index < _in.Systems().size(); ++index)
    {
      const SystemId system{static_cast<std::int32_t>(index)};

      // Arrival strength per player, at end of movement.
      std::vector<std::uint32_t> arriving(_in.Players().size(), 0);
      for (const MatchFleet& fleet : _in.Fleets())
      {
        if (!fleet.destroyed && fleet.arrivedThisTick && fleet.at == system && fleet.owner.IsValid())
        {
          arriving[fleet.owner.AsSize()] += fleet.ships;
        }
      }

      std::vector<FleetId> departed;
      for (std::size_t fleetIndex = 0; fleetIndex < _in.Fleets().size(); ++fleetIndex)
      {
        const MatchFleet& fleet = _in.Fleets()[fleetIndex];
        if (!fleet.destroyed && fleet.departedFrom == system)
        {
          departed.emplace_back(static_cast<std::int32_t>(fleetIndex));
        }
      }

      for (const FleetId leaving : departed)
      {
        const PlayerId owner = _in.FleetAt(leaving).owner;

        std::uint64_t hostileStrength = 0;
        for (std::size_t player = 0; player < arriving.size(); ++player)
        {
          if (PlayerId{static_cast<std::int32_t>(player)} != owner)
          {
            hostileStrength += arriving[player];
          }
        }
        if (hostileStrength == 0)
        {
          continue;
        }

        // One round, at the ordinary rate, with no defender bonus for anyone: the departing fleet
        // is not an incumbent any more and the arrivals have not landed on anything to defend.
        const auto damage = static_cast<std::uint32_t>((hostileStrength * _in.Rules().damagePercentPerRound) / 100);
        MatchFleet& fleet = next.MutableFleets()[leaving.AsSize()];
        const std::uint32_t lost = std::min(damage, fleet.ships);
        fleet.ships -= lost;

        record.lines.push_back(
          std::format("rear-guard at {}: fleet {} lost {} on its way out", NameOf(_in, system), leaving.Index(), lost));

        if (fleet.ships == 0)
        {
          fleet.destroyed = true;
          fleet.at = SystemId{};
          fleet.movingFrom = SystemId{};
          fleet.movingTo = SystemId{};
          fleet.ticksRemaining = 0;
        }

        Tell(_log, owner,
             DigestEntry{.kind = DigestKind::Battle,
                         .severity = Severity::BATTLE,
                         .title = std::format("Rear-guard action at {}", NameOf(_in, system)),
                         .detail = std::format("Lost {} covering the withdrawal", lost),
                         .system = system,
                         .fleet = leaving});
      }
    }
  }

  // ---- 4b, system combat -----------------------------------------------------------------------
  //
  // Read from the post-4a state -- which here is `next`, since 4a wrote into it -- at every system
  // holding hostile fleets. ADR-021 has the arithmetic and the reasoning; what matters at this
  // level is that a round is computed entirely from ROUND-START strengths and applied afterwards,
  // so neither side gets to shoot first.
  for (std::size_t index = 0; index < next.Systems().size(); ++index)
  {
    const SystemId system{static_cast<std::int32_t>(index)};

    // Sides, in player order. Never a map: two machines have to fight the same battle.
    const std::size_t playerCount = next.Players().size();
    std::vector<std::uint32_t> ships(playerCount, 0);
    std::vector<bool> incumbent(playerCount, false);
    std::vector<std::vector<FleetId>> fleetsOf(playerCount);

    for (std::size_t fleetIndex = 0; fleetIndex < next.Fleets().size(); ++fleetIndex)
    {
      const MatchFleet& fleet = next.Fleets()[fleetIndex];
      if (fleet.destroyed || fleet.InTransit() || fleet.at != system || !fleet.owner.IsValid() || fleet.ships == 0)
      {
        continue;
      }
      const std::size_t side = fleet.owner.AsSize();
      ships[side] += fleet.ships;
      fleetsOf[side].emplace_back(static_cast<std::int32_t>(fleetIndex));

      // An incumbent is a fleet that was ALREADY HERE. Not the system's owner: at an empty system
      // nobody owns anything, and the one-pager still says simultaneous arrivals get no bonus.
      if (!fleet.arrivedThisTick)
      {
        incumbent[side] = true;
      }
    }

    std::vector<std::size_t> sides;
    for (std::size_t side = 0; side < playerCount; ++side)
    {
      if (ships[side] > 0)
      {
        sides.push_back(side);
      }
    }
    if (sides.size() < 2)
    {
      continue;
    }

    std::vector<std::uint32_t> before = ships;

    for (std::uint32_t round = 0; round < next.Rules().combatRounds; ++round)
    {
      // Effective strength, with the incumbent's bonus. Computed fresh each round from the
      // survivors, so a side that is losing also hits less hard.
      std::vector<std::uint64_t> effective(playerCount, 0);
      std::uint64_t total = 0;
      for (const std::size_t side : sides)
      {
        const std::uint64_t bonus = incumbent[side] ? next.Rules().defenderBonusPercent : 100U;
        effective[side] = (static_cast<std::uint64_t>(ships[side]) * bonus) / 100U;
        total += effective[side];
      }

      // Damage each side deals, and where it lands: spread across the enemies in proportion to
      // THEIR strength, which is the one-pager's rule and means a big enemy soaks more of it.
      std::vector<std::uint64_t> incoming(playerCount, 0);
      for (const std::size_t attacker : sides)
      {
        const std::uint64_t output = (effective[attacker] * next.Rules().damagePercentPerRound) / 100U;
        const std::uint64_t enemies = total - effective[attacker];
        if (output == 0 || enemies == 0)
        {
          continue;
        }
        for (const std::size_t defender : sides)
        {
          if (defender != attacker)
          {
            incoming[defender] += (output * effective[defender]) / enemies;
          }
        }
      }

      // Applied only now. Every number above came from the state at the start of the round, so a
      // tie is a tie rather than a race -- "a tie is mutual attrition, not a coin flip".
      bool anyLoss = false;
      for (const std::size_t side : sides)
      {
        const auto lost = static_cast<std::uint32_t>(std::min<std::uint64_t>(incoming[side], ships[side]));
        ships[side] -= lost;
        anyLoss = anyLoss || lost > 0;
      }

      std::size_t standing = 0;
      for (const std::size_t side : sides)
      {
        standing += ships[side] > 0 ? 1 : 0;
      }
      if (standing < 2 || !anyLoss)
      {
        // Either it is over, or nobody can hurt anybody and further rounds would change nothing.
        break;
      }
    }

    // Losses back onto the fleets, largest first so the remainder falls on the smallest, and by
    // fleet id within equal sizes -- a total order, because which fleet absorbs a loss changes what
    // survives (ADR-018).
    for (const std::size_t side : sides)
    {
      std::uint32_t toLose = before[side] - ships[side];
      if (toLose == 0)
      {
        continue;
      }

      std::vector<FleetId> order = fleetsOf[side];
      std::sort(order.begin(), order.end(),
                [&next](FleetId _left, FleetId _right)
                {
                  const std::uint32_t leftShips = next.FleetAt(_left).ships;
                  const std::uint32_t rightShips = next.FleetAt(_right).ships;
                  if (leftShips != rightShips)
                  {
                    return leftShips > rightShips;
                  }
                  return _left < _right;
                });

      for (const FleetId fleetId : order)
      {
        MatchFleet& fleet = next.MutableFleets()[fleetId.AsSize()];
        const std::uint32_t lost = std::min(toLose, fleet.ships);
        fleet.ships -= lost;
        toLose -= lost;
        if (fleet.ships == 0)
        {
          fleet.destroyed = true;
          fleet.at = SystemId{};
        }
        if (toLose == 0)
        {
          break;
        }
      }

      const PlayerId owner{static_cast<std::int32_t>(side)};
      Tell(_log, owner,
           DigestEntry{.kind = DigestKind::Battle,
                       .severity = Severity::BATTLE,
                       .title = std::format("Battle at {}", NameOf(next, system)),
                       .detail =
                         std::format("{} of {} lost{}", before[side] - ships[side], before[side], incumbent[side] ? " (defending)" : ""),
                       .system = system});
    }

    // A hold-fire agreement is not enforced -- nothing in this game is, which is the point of
    // "no enforced treaties" -- so the only thing that happens when one is broken is that both
    // sides are told, in as many words. The sanction is entirely social, and it works because the
    // digest is the screen everybody reads.
    for (std::size_t attacker : sides)
    {
      for (std::size_t victim : sides)
      {
        const PlayerId one{static_cast<std::int32_t>(attacker)};
        const PlayerId other{static_cast<std::int32_t>(victim)};
        if (attacker >= victim || !next.HasAgreement(AgreementKind::HoldFire, one, other))
        {
          continue;
        }

        record.lines.push_back(std::format("{} and {} fought under a hold agreement", NameOf(one), NameOf(other)));
        for (const PlayerId side : {one, other})
        {
          Tell(_log, side,
               DigestEntry{.kind = DigestKind::AgreementBreached,
                           .severity = Severity::AGREEMENT_BREACHED,
                           .title = "Hold agreement broken",
                           .detail = std::format("Fighting at {}, and nothing enforced it", NameOf(next, system)),
                           .system = system,
                           .other = side == one ? other : one});
        }
      }
    }

    std::string report = std::format("battle at {}:", NameOf(next, system));
    for (const std::size_t side : sides)
    {
      report += std::format(" {} {}->{}", NameOf(PlayerId{static_cast<std::int32_t>(side)}), before[side], ships[side]);
    }
    record.lines.push_back(report);
  }

  return next;
}

// ---- Phase 5 -- claims and captures ----------------------------------------------------------------
//
// The one-pager, *Tick resolution* §5, in three sentences: a claim needs presence uncontested by
// any surviving hostile at the end of the tick; taking an OWNED system needs two consecutive such
// ticks, siege then capture; a fleet that arrived and died contests nothing -- which is automatic
// here, because a destroyed fleet is not present.

Match TickResolver::Claim(const Match& _in, TickLog& _log)
{
  Match next = _in;
  PhaseRecord& record = OpenPhase(_log, Phase::Claims);

  for (std::size_t index = 0; index < _in.Systems().size(); ++index)
  {
    const SystemId system{static_cast<std::int32_t>(index)};
    const SystemState& before = _in.SystemAt(system);
    SystemState& after = next.MutableSystems()[index];

    // The sealed region can be raided, not claimed (one-pager). Nothing here touches it.
    if (_in.GalaxyGraph().SystemAt(system).kind == SystemKind::RegionAnchor)
    {
      continue;
    }

    const std::vector<PlayerId> present = OwnersPresent(_in, system);

    std::vector<PlayerId> hostiles;
    for (const PlayerId who : present)
    {
      if (who != before.owner)
      {
        hostiles.push_back(who);
      }
    }

    // Unowned: one player alone takes it. Two or more and it stays open -- the one-pager's
    // "occupied, unclaimed".
    if (!before.owner.IsValid())
    {
      if (present.size() == 1)
      {
        after.owner = present.front();
        after.capturedAt = _in.Tick();
        after.siegeBy = PlayerId{};
        after.siegeTicks = 0;

        record.lines.push_back(std::format("{} claimed {}", NameOf(present.front()), NameOf(_in, system)));
        Tell(_log, present.front(),
             DigestEntry{.kind = DigestKind::SystemClaimed,
                         .severity = Severity::TOOK_A_SYSTEM,
                         .title = std::format("Claimed {}", NameOf(_in, system)),
                         .detail = "It was unclaimed and uncontested",
                         .system = system});
      }
      else if (present.size() > 1)
      {
        record.lines.push_back(std::format("{} is occupied and unclaimed", NameOf(_in, system)));
      }
      continue;
    }

    // Owned. A single hostile, with the owner absent and the guard not up, makes progress.
    const bool ownerPresent = std::find(present.begin(), present.end(), before.owner) != present.end();
    const bool guarded = _in.IsCapitalGuarded(system);

    if (hostiles.size() != 1 || ownerPresent || guarded)
    {
      if (before.siegeTicks > 0)
      {
        record.lines.push_back(std::format("the siege of {} was broken", NameOf(_in, system)));
      }
      if (guarded && !hostiles.empty())
      {
        record.lines.push_back(std::format("{} is a guarded capital", NameOf(_in, system)));
      }
      after.siegeBy = PlayerId{};
      after.siegeTicks = 0;
      continue;
    }

    const PlayerId besieger = hostiles.front();
    const std::uint32_t held = before.siegeBy == besieger ? before.siegeTicks + 1 : 1;

    if (held >= _in.Rules().siegeTicks)
    {
      const PlayerId loser = before.owner;
      after.owner = besieger;
      after.capturedAt = _in.Tick();
      after.siegeBy = PlayerId{};
      after.siegeTicks = 0;

      record.lines.push_back(std::format("{} captured {} from {}", NameOf(besieger), NameOf(_in, system), NameOf(loser)));
      Tell(_log, besieger,
           DigestEntry{.kind = DigestKind::SystemClaimed,
                       .severity = Severity::TOOK_A_SYSTEM,
                       .title = std::format("Captured {}", NameOf(_in, system)),
                       .detail = std::format("Held uncontested for {} ticks", _in.Rules().siegeTicks),
                       .system = system,
                       .other = loser});
      Tell(_log, loser,
           DigestEntry{.kind = DigestKind::SystemLost,
                       .severity = Severity::LOST_A_SYSTEM,
                       .title = std::format("Lost {}", NameOf(_in, system)),
                       .detail = std::format("Taken by {}", NameOf(besieger)),
                       .system = system,
                       .other = besieger});
    }
    else
    {
      after.siegeBy = besieger;
      after.siegeTicks = held;

      record.lines.push_back(std::format("{} is under siege", NameOf(_in, system)));
      Tell(_log, before.owner,
           DigestEntry{.kind = DigestKind::SiegeBegun,
                       .severity = Severity::UNDER_SIEGE,
                       .title = std::format("{} under siege", NameOf(_in, system)),
                       .detail = std::format("It falls next tick unless the siege is broken"),
                       .system = system,
                       .other = besieger});
    }
  }

  // Trade lanes cancel when an endpoint changes hands. The one-pager insists the digest
  // distinguishes this from a partner walking away, and both sides are told.
  {
    std::vector<ActiveTradeLane> surviving;
    for (const ActiveTradeLane& lane : next.TradeLanes())
    {
      if (TradeLaneStillValid(next, lane))
      {
        surviving.push_back(lane);
        continue;
      }

      record.lines.push_back("a trade lane canceled: system lost");
      for (const PlayerId side : {lane.a, lane.b})
      {
        Tell(_log, side,
             DigestEntry{.kind = DigestKind::LaneCanceled,
                         .severity = Severity::LANE_CHANGED,
                         .title = "Trade lane canceled",
                         .detail = "System lost",
                         .lane = lane.lane,
                         .other = side == lane.a ? lane.b : lane.a});
      }
    }
    next.MutableTradeLanes() = surviving;
  }

  // ---- First contact ---------------------------------------------------------------------------
  //
  // "First contact raises the game's own prompt: 'Contact: [player]. Propose trade lane?' -- one
  // tap, no text." Raised here because it depends on who owns what, which phase 5 has just
  // settled, and raised ONCE per pair: an offer arriving every six hours forever is a notification
  // stream, and this game has one digest a tick and no notifications.
  for (std::size_t index = 0; index < next.GalaxyGraph().Lanes().size(); ++index)
  {
    const GalaxyLane& lane = next.GalaxyGraph().Lanes()[index];
    const PlayerId first = next.SystemAt(lane.a).owner;
    const PlayerId second = next.SystemAt(lane.b).owner;

    if (!first.IsValid() || !second.IsValid() || first == second || next.HaveMet(first, second))
    {
      continue;
    }

    const PlayerId low = first < second ? first : second;
    const PlayerId high = first < second ? second : first;
    next.MutableContacts().push_back(Contact{.a = low, .b = high, .tick = _in.Tick()});

    record.lines.push_back(std::format("{} and {} have met", NameOf(low), NameOf(high)));
    for (const PlayerId side : {low, high})
    {
      const PlayerId other = side == low ? high : low;
      Tell(_log, side,
           DigestEntry{.kind = DigestKind::Contact,
                       .severity = Severity::FIRST_CONTACT,
                       .title = std::format("Contact: {}", NameOf(other)),
                       .detail = "Propose trade lane?",
                       .lane = LaneId{static_cast<std::int32_t>(index)},
                       .other = other});
    }
  }

  return next;
}

// ---- Phase 6 -- digest ------------------------------------------------------------------------------

void TickResolver::WriteDigest(const Match& _in, TickLog& _log)
{
  PhaseRecord& record = OpenPhase(_log, Phase::Digest);

  // ADR-020: sorted by the severity each event carries, most consequential first. `stable_sort`
  // plus a total tie-break on the ids keeps the order identical on every machine -- two events of
  // equal severity must not be able to swap places between two runs (ADR-018).
  for (std::vector<DigestEntry>& digest : _log.digests)
  {
    std::stable_sort(digest.begin(), digest.end(),
                     [](const DigestEntry& _left, const DigestEntry& _right)
                     {
                       if (_left.severity != _right.severity)
                       {
                         return _left.severity > _right.severity;
                       }
                       if (_left.kind != _right.kind)
                       {
                         return _left.kind < _right.kind;
                       }
                       if (_left.system != _right.system)
                       {
                         return _left.system < _right.system;
                       }
                       return _left.lane < _right.lane;
                     });
  }

  std::uint32_t total = 0;
  for (const std::vector<DigestEntry>& digest : _log.digests)
  {
    total += static_cast<std::uint32_t>(digest.size());
  }
  record.lines.push_back(std::format("{} events across {} players", total, _in.Players().size()));
}

} // namespace Frontier
