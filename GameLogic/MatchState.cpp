// MatchState.cpp -- the graph queries the generator and the resolver both ask.

#include "pch.h"
#include "MatchState.h"

#include <limits>

namespace Frontier
{

std::int64_t DistanceSquaredUnits(const MapPoint& _a, const MapPoint& _b) noexcept
{
  const std::int64_t deltaX = static_cast<std::int64_t>(_a.xUnits) - _b.xUnits;
  const std::int64_t deltaY = static_cast<std::int64_t>(_a.yUnits) - _b.yUnits;
  return deltaX * deltaX + deltaY * deltaY;
}

std::int64_t LaneLengthSquaredUnits(const MatchState& _state, const Lane& _lane) noexcept
{
  return DistanceSquaredUnits(_state.systems[_lane.endA].position, _state.systems[_lane.endB].position);
}

bool AreAdjacent(const MatchState& _state, SystemId _a, SystemId _b) noexcept
{
  for (const Lane& lane : _state.lanes)
  {
    if ((lane.endA == _a && lane.endB == _b) || (lane.endA == _b && lane.endB == _a))
    {
      return true;
    }
  }
  return false;
}

std::vector<std::int32_t> ShortestPathTicksFrom(const MatchState& _state, SystemId _source)
{
  constexpr std::int32_t UNREACHED = -1;

  const std::size_t count = _state.systems.size();
  std::vector<std::int32_t> cost(count, UNREACHED);
  std::vector<bool> settled(count, false);

  if (_source >= count)
  {
    return cost;
  }
  cost[_source] = 0;

  for (std::size_t step = 0; step < count; ++step)
  {
    // Pick the cheapest unsettled system. Ascending index breaks a tie, which makes the walk order
    // a property of the state rather than of a container (R16).
    std::size_t nearest = count;
    std::int32_t nearestCost = std::numeric_limits<std::int32_t>::max();
    for (std::size_t candidate = 0; candidate < count; ++candidate)
    {
      if (!settled[candidate] && cost[candidate] != UNREACHED && cost[candidate] < nearestCost)
      {
        nearest = candidate;
        nearestCost = cost[candidate];
      }
    }

    if (nearest == count)
    {
      break;
    }
    settled[nearest] = true;

    for (const Lane& lane : _state.lanes)
    {
      SystemId other = NO_SYSTEM;
      if (lane.endA == nearest)
      {
        other = lane.endB;
      }
      else if (lane.endB == nearest)
      {
        other = lane.endA;
      }
      else
      {
        continue;
      }

      const std::int32_t reached = nearestCost + lane.costTicks;
      if (cost[other] == UNREACHED || reached < cost[other])
      {
        cost[other] = reached;
      }
    }
  }

  return cost;
}

} // namespace Frontier
