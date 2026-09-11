// BotPolicy.cpp -- how a seat plays itself.
//
// Lifted from `Tests/GameLogicTests/ScriptedMatchTests.cpp`, where these bots were written to prove
// the core loop worked before anybody played it (ADR-037). The test file now drives these, so the
// bot the suite runs a whole match with is the bot a host puts in a seat.

#include "pch.h"
#include "BotPolicy.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace Lockstep
{

namespace
{

/// Systems this player holds and can see right now.
[[nodiscard]] std::vector<SystemId> Held(const Snapshot& _view)
{
  std::vector<SystemId> mine;
  for (const SnapshotSystem& system : _view.Systems())
  {
    if (system.live && system.owner == _view.Viewer())
    {
      mine.push_back(system.id);
    }
  }
  return mine;
}

/// Every lane out of `_from` the player knows about, as (destination, cost), lowest id first.
[[nodiscard]] std::vector<std::pair<SystemId, std::uint32_t>> Exits(const Snapshot& _view, SystemId _from)
{
  std::vector<std::pair<SystemId, std::uint32_t>> out;
  for (const SnapshotLane& lane : _view.Lanes())
  {
    if (lane.a == _from)
    {
      out.emplace_back(lane.b, lane.costTicks);
    }
    else if (lane.b == _from)
    {
      out.emplace_back(lane.a, lane.costTicks);
    }
  }
  return out;
}

/// Where this fleet should go, by policy. An invalid id means hold.
///
/// A breadth-first walk over the lanes the player KNOWS ABOUT, to the nearest (or furthest) system
/// worth having, returning the first hop toward it. An earlier version of these bots looked only one
/// lane ahead, and every one of them stalled the moment it ran out of adjacent open ground -- six
/// empires sat on two systems each for eighty ticks and never met. That was a harness failure that
/// looked exactly like a rules failure, which is worth remembering: a bot too simple to reach a
/// mechanic will report that the mechanic does not work.
[[nodiscard]] SystemId ChooseDestination(BotPolicy _policy, const Snapshot& _view, SystemId _at)
{
  if (_policy == BotPolicy::Turtle)
  {
    return {};
  }

  // Highest system id the player knows about, so the walk can be indexed rather than searched.
  std::int32_t highest = _at.Index();
  for (const SnapshotSystem& system : _view.Systems())
  {
    highest = std::max(highest, system.id.Index());
  }
  const std::size_t count = static_cast<std::size_t>(highest) + 1;

  std::vector<std::vector<SystemId>> exits(count);
  for (const SnapshotLane& lane : _view.Lanes())
  {
    if (lane.a.AsSize() < count && lane.b.AsSize() < count)
    {
      exits[lane.a.AsSize()].push_back(lane.b);
      exits[lane.b.AsSize()].push_back(lane.a);
    }
  }
  for (std::vector<SystemId>& row : exits)
  {
    std::sort(row.begin(), row.end());
  }

  std::vector<std::int32_t> distance(count, -1);
  std::vector<SystemId> firstHop(count);
  distance[_at.AsSize()] = 0;

  std::vector<SystemId> ring = {_at};
  std::int32_t depth = 0;
  while (!ring.empty())
  {
    std::vector<SystemId> next;
    for (const SystemId here : ring)
    {
      for (const SystemId other : exits[here.AsSize()])
      {
        if (distance[other.AsSize()] >= 0)
        {
          continue;
        }
        distance[other.AsSize()] = depth + 1;
        firstHop[other.AsSize()] = depth == 0 ? other : firstHop[here.AsSize()];
        next.push_back(other);
      }
    }
    ring = next;
    ++depth;
  }

  SystemId best;
  std::int32_t bestDistance = 0;
  bool bestIsRival = false;

  for (const SnapshotSystem& system : _view.Systems())
  {
    if (system.kind == SystemKind::RegionAnchor || system.id == _at)
    {
      continue;
    }
    const std::size_t index = system.id.AsSize();
    if (index >= count || distance[index] < 0 || !firstHop[index].IsValid())
    {
      continue;
    }

    const bool unowned = !system.owner.IsValid();
    const bool rival = system.owner.IsValid() && system.owner != _view.Viewer();
    if (!unowned && !(rival && _policy == BotPolicy::Raider))
    {
      continue;
    }

    // The raider goes for somebody else's ground first and open ground only when there is none.
    // Everyone else takes what is open, near or far by policy.
    const bool preferred = _policy == BotPolicy::Raider && rival;
    bool better = !best.IsValid();
    if (!better && _policy == BotPolicy::Raider && preferred != bestIsRival)
    {
      better = preferred;
    }
    else if (!better && distance[index] != bestDistance)
    {
      better = _policy == BotPolicy::ExpandFar ? distance[index] > bestDistance : distance[index] < bestDistance;
    }
    else if (!better)
    {
      better = system.id < best;
    }

    if (better)
    {
      best = system.id;
      bestDistance = distance[index];
      bestIsRival = preferred;
    }
  }

  return best.IsValid() ? firstHop[best.AsSize()] : SystemId{};
}

} // namespace

OrderSet BotOrdersFor(BotPolicy _policy, const Snapshot& _view, const MatchRules& _rules)
{
  OrderSet orders;
  orders.player = _view.Viewer();

  if (_policy == BotPolicy::Absentee)
  {
    return orders;
  }

  // Answer everything addressed to you. A trade lane is free income and the other two cost nothing,
  // so a bot that declined would be modelling suspicion the design has no mechanic for.
  for (const SnapshotProposal& proposal : _view.Proposals())
  {
    if (proposal.to == _view.Viewer())
    {
      orders.answers.push_back(AnswerOrder{.proposal = proposal.id, .answer = Answer::Accept});
    }
  }

  // Move whatever is parked.
  for (const SnapshotFleet& fleet : _view.Fleets())
  {
    if (fleet.owner != _view.Viewer() || fleet.ticksRemaining > 0 || !fleet.at.IsValid())
    {
      continue;
    }

    const SystemId destination = ChooseDestination(_policy, _view, fleet.at);
    orders.fleetOrders.push_back(FleetOrder{.fleet = fleet.id, .destination = destination.IsValid() ? destination : fleet.at});
  }

  // Build, cheapest useful thing first, on the lowest-numbered system that lacks one. The turtle
  // builds shipyards because it never moves; everybody else builds income.
  const std::vector<SystemId> mine = Held(_view);
  const bool yardFirst = _policy == BotPolicy::Turtle;

  for (const SystemId system : mine)
  {
    const SnapshotSystem* state = _view.System(system);
    if (state == nullptr)
    {
      continue;
    }

    if (yardFirst && !state->hasShipyard)
    {
      orders.builds.push_back(BuildOrder{.system = system, .kind = BuildKind::Shipyard});
      break;
    }
    if (!yardFirst && !state->hasMiningStation)
    {
      orders.builds.push_back(BuildOrder{.system = system, .kind = BuildKind::MiningStation});
      break;
    }
  }

  // The diplomat offers a lane wherever its territory touches somebody else's and nothing is open
  // or pending there. One offer a tick, so it does not flood the board.
  if (_policy == BotPolicy::Diplomat)
  {
    for (const SnapshotLane& lane : _view.Lanes())
    {
      if (lane.tradeLane)
      {
        continue;
      }

      const SnapshotSystem* first = _view.System(lane.a);
      const SnapshotSystem* second = _view.System(lane.b);
      if (first == nullptr || second == nullptr || !first->live || !second->live)
      {
        continue;
      }

      const bool mineFirst = first->owner == _view.Viewer();
      const bool mineSecond = second->owner == _view.Viewer();
      if (mineFirst == mineSecond)
      {
        continue;
      }

      const SnapshotSystem* theirs = mineFirst ? second : first;
      if (!theirs->owner.IsValid())
      {
        continue;
      }

      const bool pending = std::any_of(_view.Proposals().begin(), _view.Proposals().end(),
                                       [&lane](const SnapshotProposal& _open) { return _open.lane == lane.id; });
      if (pending)
      {
        continue;
      }

      if (_view.Standings()[_view.Viewer().AsSize()].score > 0)
      {
        orders.proposals.push_back(ProposalOrder{.to = theirs->owner, .kind = ProposalKind::OpenLane, .lane = lane.id});
      }
      break;
    }
  }

  (void)_rules;
  return orders;
}

} // namespace Lockstep
