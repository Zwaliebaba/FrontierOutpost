// Match.cpp -- the authoritative state, and the rule that decides whether an order is real.
//
// Validation is the substance of this file. It is written once and read twice: the resolver
// applies it at the lock, and the client will be shown the same answer before the lock so that
// nobody is offered a move that cannot happen. The one-pager makes that a rule about the whole
// system rather than a nicety -- "nobody is ever shown a dead offer as acceptable" -- and the way
// to keep it true is to have exactly one function that decides.

#include "pch.h"
#include "Match.h"

#include "GalaxyGenerator.h"

namespace Frontier
{

namespace
{

/// FNV-1a, the same mixer the tests use on a galaxy. It is a hash for *equality of state*, not a
/// checksum against tampering, and 64 bits of it over a few thousand fields is far past what a
/// test needs to tell two matches apart.
class StateHash
{
public:
  void Absorb(std::uint64_t _value) noexcept
  {
    for (std::int32_t byte = 0; byte < 8; ++byte)
    {
      m_hash ^= (_value >> (byte * 8)) & 0xFFULL;
      m_hash *= 0x100000001B3ULL;
    }
  }

  void AbsorbId(std::int32_t _index) noexcept
  {
    Absorb(static_cast<std::uint64_t>(_index));
  }

  [[nodiscard]] std::uint64_t Value() const noexcept
  {
    return m_hash;
  }

private:
  std::uint64_t m_hash = 0xCBF29CE484222325ULL;
};

} // namespace

const char* Describe(PlayerStatus _status) noexcept
{
  switch (_status)
  {
  case PlayerStatus::Active:
    return "active";
  case PlayerStatus::Custodian:
    return "custodian";
  case PlayerStatus::Exile:
    return "exile";
  case PlayerStatus::Gone:
    return "gone";
  default:
    return "unknown";
  }
}

Match Match::Create(const MatchRules& _rules, std::uint64_t _seed)
{
  // Rules that contradict the game they are rules for are a caller defect, not a state to recover
  // from. `Check` is where the reasoning lives (MatchRules.h) and it is separate from here so it
  // can be asked before anything is built -- and so it is testable without provoking this.
  if (const RulesProblem problem = Check(_rules); problem != RulesProblem::None)
  {
    Neuron::Fatal("These rules cannot be played: {}.", Describe(problem));
  }

  Match match;
  match.m_rules = _rules;

  const GeneratedGalaxy generated = GalaxyGenerator::Generate(_rules, _seed);
  match.m_galaxy = generated.galaxy;
  match.m_seed = generated.seed;
  match.m_tick = 0;

  match.m_players.assign(_rules.playerCount, PlayerState{});
  for (PlayerState& player : match.m_players)
  {
    player.credits = _rules.startingCredits;
  }

  // Ownership starts as the generator left it -- capitals held, everything else open -- and from
  // here on it is this vector that moves, never the galaxy.
  match.m_systems.reserve(match.m_galaxy.Systems().size());
  for (const GalaxySystem& system : match.m_galaxy.Systems())
  {
    SystemState state;
    state.owner = system.owner;
    match.m_systems.push_back(state);
  }

  match.m_seen.assign(_rules.playerCount, std::vector<SeenSystem>(match.m_systems.size()));

  // One fleet each, on the capital. In capital order, so fleet ids run with player ids and a
  // reader can predict which is whose.
  for (const SystemId capital : match.m_galaxy.Capitals())
  {
    MatchFleet fleet;
    fleet.owner = match.m_galaxy.SystemAt(capital).owner;
    fleet.ships = _rules.startingShips;
    fleet.at = capital;
    (void)match.AddFleet(fleet);
  }

  // What everybody can see from where they start. Without this a snapshot taken before the first
  // lock is a blank map -- including the player's own capital.
  match.RecomputeVisibility();
  return match;
}

void Match::RecomputeVisibility()
{
  const std::size_t systemCount = m_systems.size();
  std::vector<std::vector<bool>> live(m_players.size(), std::vector<bool>(systemCount, false));

  const auto lightUp = [this, &live, systemCount](std::size_t _player, SystemId _from)
  {
    if (!_from.IsValid() || _from.AsSize() >= systemCount)
    {
      return;
    }
    live[_player][_from.AsSize()] = true;
    if (m_rules.scoutingRangeLanes == 0)
    {
      return;
    }
    for (const LaneId lane : m_galaxy.LanesAt(_from))
    {
      const SystemId other = m_galaxy.OtherEnd(lane, _from);
      if (other.IsValid() && other.AsSize() < systemCount)
      {
        live[_player][other.AsSize()] = true;
      }
    }
  };

  for (std::size_t index = 0; index < systemCount; ++index)
  {
    const SystemState& state = m_systems[index];
    if (state.owner.IsValid() && state.owner.AsSize() < live.size())
    {
      lightUp(state.owner.AsSize(), SystemId{static_cast<std::int32_t>(index)});
    }
  }

  for (const MatchFleet& fleet : m_fleets)
  {
    if (fleet.destroyed || !fleet.owner.IsValid() || fleet.owner.AsSize() >= live.size())
    {
      continue;
    }
    // A fleet under way sees from both ends of the lane it is on. It is somewhere between them and
    // there is no third thing for it to be next to.
    lightUp(fleet.owner.AsSize(), fleet.at);
    lightUp(fleet.owner.AsSize(), fleet.movingFrom);
    lightUp(fleet.owner.AsSize(), fleet.movingTo);
  }

  // Shared scouting, applied last and as a union, so an agreement can only ever add.
  for (const Agreement& agreement : m_agreements)
  {
    if (agreement.kind != AgreementKind::ShareScouting || !agreement.a.IsValid() || !agreement.b.IsValid())
    {
      continue;
    }
    const std::size_t first = agreement.a.AsSize();
    const std::size_t second = agreement.b.AsSize();
    if (first >= live.size() || second >= live.size())
    {
      continue;
    }
    for (std::size_t index = 0; index < systemCount; ++index)
    {
      const bool either = live[first][index] || live[second][index];
      live[first][index] = either;
      live[second][index] = either;
    }
  }

  for (std::size_t player = 0; player < m_players.size(); ++player)
  {
    std::vector<SeenSystem>& seen = m_seen[player];
    for (std::size_t index = 0; index < systemCount; ++index)
    {
      SeenSystem& record = seen[index];
      record.live = live[player][index];
      if (!record.live)
      {
        continue;
      }

      const SystemState& state = m_systems[index];
      record.known = true;
      record.asOfTick = m_tick;
      record.owner = state.owner;
      record.hadShipyard = state.hasShipyard;
      record.hadMiningStation = state.hasMiningStation;
    }
  }
}

const OpenProposal* Match::FindProposal(ProposalId _proposal) const
{
  if (!_proposal.IsValid())
  {
    return nullptr;
  }
  for (const OpenProposal& proposal : m_proposals)
  {
    if (proposal.id == _proposal)
    {
      return &proposal;
    }
  }
  return nullptr;
}

bool Match::IsTradeLane(LaneId _lane) const
{
  for (const ActiveTradeLane& lane : m_tradeLanes)
  {
    if (lane.lane == _lane)
    {
      return true;
    }
  }
  return false;
}

const ActiveTradeLane* Match::FindTradeLane(LaneId _lane) const
{
  for (const ActiveTradeLane& lane : m_tradeLanes)
  {
    if (lane.lane == _lane)
    {
      return &lane;
    }
  }
  return nullptr;
}

bool Match::HasAgreement(AgreementKind _kind, PlayerId _first, PlayerId _second) const
{
  for (const Agreement& agreement : m_agreements)
  {
    if (agreement.kind == _kind && agreement.Covers(_first, _second))
    {
      return true;
    }
  }
  return false;
}

bool Match::HaveMet(PlayerId _first, PlayerId _second) const
{
  const PlayerId low = _first < _second ? _first : _second;
  const PlayerId high = _first < _second ? _second : _first;
  for (const Contact& contact : m_contacts)
  {
    if (contact.a == low && contact.b == high)
    {
      return true;
    }
  }
  return false;
}

PlayerId Match::Leader() const
{
  PlayerId best;
  for (std::size_t index = 0; index < m_players.size(); ++index)
  {
    const PlayerId candidate{static_cast<std::int32_t>(index)};
    if (!best.IsValid() || m_players[index].score > m_players[best.AsSize()].score)
    {
      best = candidate;
    }
  }
  return best;
}

std::vector<PlayerId> Match::Placements() const
{
  std::vector<PlayerId> order;
  order.reserve(m_players.size());
  for (std::size_t index = 0; index < m_players.size(); ++index)
  {
    order.emplace_back(static_cast<std::int32_t>(index));
  }

  // ADR-023's tiebreak, in order: score, then systems held, then capitals held, then the lower
  // player id. The last one is arbitrary and is there so the order is TOTAL -- a placement that
  // depended on sort stability would differ between two machines reporting the same match.
  const auto held = [this](PlayerId _player)
  {
    std::pair<std::uint32_t, std::uint32_t> counts{0, 0};
    for (std::size_t index = 0; index < m_systems.size(); ++index)
    {
      if (m_systems[index].owner == _player)
      {
        ++counts.first;
        if (m_galaxy.SystemAt(SystemId{static_cast<std::int32_t>(index)}).kind == SystemKind::Capital)
        {
          ++counts.second;
        }
      }
    }
    return counts;
  };

  std::sort(order.begin(), order.end(),
            [this, &held](PlayerId _left, PlayerId _right)
            {
              const std::uint32_t leftScore = m_players[_left.AsSize()].score;
              const std::uint32_t rightScore = m_players[_right.AsSize()].score;
              if (leftScore != rightScore)
              {
                return leftScore > rightScore;
              }
              const auto leftHeld = held(_left);
              const auto rightHeld = held(_right);
              if (leftHeld != rightHeld)
              {
                return leftHeld > rightHeld;
              }
              return _left < _right;
            });

  return order;
}

bool Match::IsCapitalGuarded(SystemId _system) const
{
  if (!HasSystem(_system) || m_galaxy.SystemAt(_system).kind != SystemKind::Capital)
  {
    return false;
  }
  return m_tick < m_rules.capitalGuardTicks;
}

FleetId Match::AddFleet(MatchFleet _fleet)
{
  const auto index = static_cast<std::int32_t>(m_fleets.size());
  m_fleets.push_back(_fleet);
  return FleetId{index};
}

ProposalId Match::TakeNextProposalId() noexcept
{
  return ProposalId{m_nextProposalId++};
}

std::vector<RejectedOrder> Match::Validate(const OrderSet& _orders) const
{
  std::vector<RejectedOrder> rejected;

  const auto refuse = [&rejected](OrderRejection _reason, std::size_t _index)
  { rejected.push_back(RejectedOrder{.reason = _reason, .index = static_cast<std::int32_t>(_index)}); };

  if (!HasPlayer(_orders.player))
  {
    refuse(OrderRejection::NoSuchPlayer, 0);
    return rejected;
  }

  if (PlayerAt(_orders.player).status == PlayerStatus::Custodian)
  {
    // One refusal for the whole set rather than one per order. A custodian is not making mistakes;
    // its territory defends and never expands or attacks, so there are no orders to refuse
    // individually and a digest full of identical refusals would say nothing extra.
    refuse(PlayerAt(_orders.player).conceded ? OrderRejection::AlreadyConceded : OrderRejection::YouAreACustodian, 0);
    return rejected;
  }

  // ---- Fleet orders -----------------------------------------------------------------------------
  std::vector<FleetId> ordered;
  for (std::size_t index = 0; index < _orders.fleetOrders.size(); ++index)
  {
    const FleetOrder& order = _orders.fleetOrders[index];

    if (!HasFleet(order.fleet) || FleetAt(order.fleet).owner != _orders.player || FleetAt(order.fleet).destroyed)
    {
      refuse(OrderRejection::NotYourFleet, index);
      continue;
    }

    if (std::find(ordered.begin(), ordered.end(), order.fleet) != ordered.end())
    {
      refuse(OrderRejection::FleetOrderedTwice, index);
      continue;
    }
    ordered.push_back(order.fleet);

    const MatchFleet& fleet = FleetAt(order.fleet);
    if (fleet.InTransit())
    {
      refuse(OrderRejection::FleetInTransit, index);
      continue;
    }

    if (!HasSystem(order.destination))
    {
      refuse(OrderRejection::NoLaneToDestination, index);
      continue;
    }

    // Holding is an order, and the destination a fleet is already at is always reachable.
    if (order.destination == fleet.at)
    {
      continue;
    }

    bool reachable = false;
    for (const LaneId lane : m_galaxy.LanesAt(fleet.at))
    {
      if (m_galaxy.OtherEnd(lane, fleet.at) == order.destination)
      {
        reachable = true;
        break;
      }
    }
    if (!reachable)
    {
      refuse(OrderRejection::NoLaneToDestination, index);
    }
  }

  // ---- Builds -----------------------------------------------------------------------------------
  //
  // Cost is checked against the RUNNING total, not against each build alone: two twenty-credit
  // shipyards on twenty credits is one affordable order and one that is not, and the player is
  // owed the first.
  std::uint32_t spent = 0;
  const std::uint32_t purse = PlayerAt(_orders.player).credits;

  for (std::size_t index = 0; index < _orders.builds.size(); ++index)
  {
    const BuildOrder& order = _orders.builds[index];

    if (!HasSystem(order.system) || SystemAt(order.system).owner != _orders.player)
    {
      refuse(OrderRejection::NotYourSystem, index);
      continue;
    }

    const SystemState& system = SystemAt(order.system);
    const bool already = order.kind == BuildKind::Shipyard ? system.hasShipyard : system.hasMiningStation;
    if (already)
    {
      refuse(OrderRejection::AlreadyBuilt, index);
      continue;
    }

    const std::uint32_t cost = order.kind == BuildKind::Shipyard ? m_rules.shipyardCost : m_rules.miningStationCost;
    if (spent + cost > purse)
    {
      refuse(OrderRejection::CannotAfford, index);
      continue;
    }
    spent += cost;
  }

  // ---- Proposals --------------------------------------------------------------------------------
  for (std::size_t index = 0; index < _orders.proposals.size(); ++index)
  {
    const ProposalOrder& order = _orders.proposals[index];

    if (!HasPlayer(order.to) || order.to == _orders.player)
    {
      refuse(OrderRejection::NoSuchRecipient, index);
      continue;
    }

    if (order.kind == ProposalKind::OpenLane)
    {
      if (!order.lane.IsValid() || order.lane.AsSize() >= m_galaxy.Lanes().size())
      {
        refuse(OrderRejection::LaneNotBetweenYou, index);
        continue;
      }

      // A lane can only be opened between two systems the two players actually hold. Anything else
      // is an offer about somebody else's territory, which is the "dead offer" the one-pager will
      // not have shown as acceptable.
      const GalaxyLane& lane = m_galaxy.LaneAt(order.lane);
      const PlayerId first = SystemAt(lane.a).owner;
      const PlayerId second = SystemAt(lane.b).owner;

      const bool joinsThem = (first == _orders.player && second == order.to) || (second == _orders.player && first == order.to);
      if (!joinsThem)
      {
        refuse(OrderRejection::LaneNotBetweenYou, index);
        continue;
      }

      if (spent + m_rules.tradeLaneCost > purse)
      {
        refuse(OrderRejection::CannotAfford, index);
        continue;
      }
      spent += m_rules.tradeLaneCost;
    }

    if (order.kind == ProposalKind::HoldForTicks && (order.ticks == 0 || order.ticks > m_rules.proposalWindowTicks))
    {
      refuse(OrderRejection::BadHoldWindow, index);
      continue;
    }

    // A conditional lane is a second offer riding on the first, so it is held to the same rule:
    // it has to join these two empires, or accepting would open a lane across somebody else's
    // territory.
    if (order.conditionalLane.IsValid())
    {
      if (order.conditionalLane.AsSize() >= m_galaxy.Lanes().size())
      {
        refuse(OrderRejection::ConditionalLaneNotBetweenYou, index);
        continue;
      }
      const GalaxyLane& conditional = m_galaxy.LaneAt(order.conditionalLane);
      const PlayerId first = SystemAt(conditional.a).owner;
      const PlayerId second = SystemAt(conditional.b).owner;
      if (!((first == _orders.player && second == order.to) || (second == _orders.player && first == order.to)))
      {
        refuse(OrderRejection::ConditionalLaneNotBetweenYou, index);
      }
    }
  }

  // ---- Answers and withdrawals ------------------------------------------------------------------
  for (std::size_t index = 0; index < _orders.answers.size(); ++index)
  {
    const OpenProposal* proposal = FindProposal(_orders.answers[index].proposal);
    if (proposal == nullptr)
    {
      refuse(OrderRejection::NoSuchProposal, index);
    }
    else if (proposal->to != _orders.player)
    {
      refuse(OrderRejection::NotYoursToAnswer, index);
    }
  }

  for (std::size_t index = 0; index < _orders.withdrawals.size(); ++index)
  {
    const OpenProposal* proposal = FindProposal(_orders.withdrawals[index].proposal);
    if (proposal == nullptr)
    {
      refuse(OrderRejection::NoSuchProposal, index);
    }
    else if (proposal->from != _orders.player)
    {
      refuse(OrderRejection::NotYoursToWithdraw, index);
    }
  }

  // Either party may cancel, which is the one-pager's rule and the reason cancelling is a tell.
  // What nobody may do is cancel a lane they are not on.
  for (std::size_t index = 0; index < _orders.cancellations.size(); ++index)
  {
    const ActiveTradeLane* lane = FindTradeLane(_orders.cancellations[index].lane);
    if (lane == nullptr || (lane->a != _orders.player && lane->b != _orders.player))
    {
      refuse(OrderRejection::NotYourTradeLane, index);
    }
  }

  return rejected;
}

std::uint64_t Match::Hash() const
{
  StateHash hash;
  hash.Absorb(m_seed);
  hash.Absorb(m_tick);
  hash.Absorb(m_galaxy.SystemCount());
  hash.Absorb(m_galaxy.LaneCount());

  for (const SystemState& system : m_systems)
  {
    hash.AbsorbId(system.owner.Index());
    hash.Absorb(system.hasShipyard ? 1U : 0U);
    hash.Absorb(system.hasMiningStation ? 1U : 0U);
    hash.AbsorbId(system.siegeBy.Index());
    hash.Absorb(system.siegeTicks);
    hash.Absorb(system.capturedAt);
    hash.Absorb(system.halfYield ? 1U : 0U);
  }

  for (const MatchFleet& fleet : m_fleets)
  {
    hash.AbsorbId(fleet.owner.Index());
    hash.Absorb(fleet.ships);
    hash.AbsorbId(fleet.at.Index());
    hash.AbsorbId(fleet.movingFrom.Index());
    hash.AbsorbId(fleet.movingTo.Index());
    hash.Absorb(fleet.ticksRemaining);
    hash.AbsorbId(fleet.orderedTo.Index());
    hash.Absorb(fleet.arrivedThisTick ? 1U : 0U);
    hash.AbsorbId(fleet.departedFrom.Index());
    hash.Absorb(fleet.destroyed ? 1U : 0U);
  }

  for (const PlayerState& player : m_players)
  {
    hash.Absorb(player.credits);
    hash.Absorb(player.score);
    hash.Absorb(static_cast<std::uint64_t>(player.status));
    hash.Absorb(player.lastActiveTick);
    hash.Absorb(player.absentTicks);
    hash.Absorb(player.custodianSince);
    hash.Absorb(player.conceded ? 1U : 0U);
    hash.Absorb(player.forfeitedScore ? 1U : 0U);
    hash.Absorb(player.dominanceTicks);
  }

  for (const OpenProposal& proposal : m_proposals)
  {
    hash.AbsorbId(proposal.id.Index());
    hash.AbsorbId(proposal.from.Index());
    hash.AbsorbId(proposal.to.Index());
    hash.Absorb(static_cast<std::uint64_t>(proposal.kind));
    hash.AbsorbId(proposal.lane.Index());
    hash.AbsorbId(proposal.conditionalLane.Index());
    hash.Absorb(proposal.ticks);
    hash.Absorb(proposal.openedAt);
  }

  for (const ActiveTradeLane& lane : m_tradeLanes)
  {
    hash.AbsorbId(lane.lane.Index());
    hash.AbsorbId(lane.a.Index());
    hash.AbsorbId(lane.b.Index());
    hash.Absorb(lane.openedAt);
  }

  for (const Agreement& agreement : m_agreements)
  {
    hash.Absorb(static_cast<std::uint64_t>(agreement.kind));
    hash.AbsorbId(agreement.a.Index());
    hash.AbsorbId(agreement.b.Index());
    hash.Absorb(agreement.openedAt);
    hash.Absorb(agreement.expiresAt);
  }

  for (const Contact& contact : m_contacts)
  {
    hash.AbsorbId(contact.a.Index());
    hash.AbsorbId(contact.b.Index());
    hash.Absorb(contact.tick);
  }

  // What each player knows is state like any other. A snapshot is built from it, so two machines
  // that disagreed about it would show two different maps of the same match.
  for (const std::vector<SeenSystem>& byPlayer : m_seen)
  {
    for (const SeenSystem& seen : byPlayer)
    {
      hash.Absorb(seen.live ? 1U : 0U);
      hash.Absorb(seen.known ? 1U : 0U);
      hash.Absorb(seen.asOfTick);
      hash.AbsorbId(seen.owner.Index());
      hash.Absorb(seen.hadShipyard ? 1U : 0U);
      hash.Absorb(seen.hadMiningStation ? 1U : 0U);
    }
  }

  hash.AbsorbId(m_dominanceWinner.Index());
  return hash.Value();
}

bool Match::IsConsistent() const
{
  if (m_systems.size() != m_galaxy.Systems().size())
  {
    return false;
  }

  for (const SystemState& system : m_systems)
  {
    if (system.owner.IsValid() && !HasPlayer(system.owner))
    {
      return false;
    }
    // A siege is a besieger and a count, together or not at all.
    if (system.siegeBy.IsValid() != (system.siegeTicks > 0))
    {
      return false;
    }
  }

  for (const MatchFleet& fleet : m_fleets)
  {
    if (fleet.destroyed)
    {
      continue;
    }
    if (!HasPlayer(fleet.owner))
    {
      return false;
    }
    // Parked or under way, never both and never neither.
    if (fleet.InTransit())
    {
      if (fleet.at.IsValid() || !fleet.movingFrom.IsValid() || !fleet.movingTo.IsValid())
      {
        return false;
      }
    }
    else if (!fleet.at.IsValid())
    {
      return false;
    }
  }

  for (const OpenProposal& proposal : m_proposals)
  {
    if (!HasPlayer(proposal.from) || !HasPlayer(proposal.to) || proposal.from == proposal.to)
    {
      return false;
    }
  }

  for (const PlayerState& player : m_players)
  {
    // Concession is custodianship that cannot be undone, so it is never a status of its own. A
    // state with one set and not the other would pass every order check that reads `status` and
    // fail every one that reads `conceded`.
    if (player.conceded && player.status != PlayerStatus::Custodian)
    {
      return false;
    }
    if (player.status == PlayerStatus::Custodian && player.custodianSince == 0 && !player.conceded)
    {
      return false;
    }
  }

  if (m_seen.size() != m_players.size())
  {
    return false;
  }
  for (const std::vector<SeenSystem>& byPlayer : m_seen)
  {
    if (byPlayer.size() != m_systems.size())
    {
      return false;
    }
  }

  return true;
}

} // namespace Frontier
