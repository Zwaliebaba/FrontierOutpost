// Galaxy.cpp -- the graph, and the two questions everything asks it: what is next to this, and how
// many ticks to there.

#include "pch.h"
#include "Galaxy.h"

#include <queue>

namespace Frontier
{

SystemId Galaxy::AddSystem(GalaxySystem _system)
{
  const auto index = static_cast<Neuron::Id<SystemTag>::Underlying>(m_systems.size());
  const SystemId id{index};

  if (_system.kind == SystemKind::RegionAnchor)
  {
    ASSERT_TEXT(!m_regionAnchor.IsValid(), L"A galaxy has one sealed region, not two.");
    m_regionAnchor = id;
  }

  m_systems.push_back(std::move(_system));
  m_lanesAt.emplace_back();
  return id;
}

LaneId Galaxy::AddLane(SystemId _a, SystemId _b, std::uint32_t _costTicks)
{
  if (!_a.IsValid() || !_b.IsValid() || _a == _b)
  {
    return LaneId{};
  }

  // Normalised so that a lane between two systems has one representation. Without it, "is there
  // already a lane here" is two comparisons and the duplicate check quietly misses half the cases.
  const SystemId low = _a < _b ? _a : _b;
  const SystemId high = _a < _b ? _b : _a;

  for (const LaneId existing : m_lanesAt[low.AsSize()])
  {
    const GalaxyLane& lane = m_lanes[existing.AsSize()];
    if (lane.a == low && lane.b == high)
    {
      return LaneId{};
    }
  }

  const auto index = static_cast<Neuron::Id<LaneTag>::Underlying>(m_lanes.size());
  const LaneId id{index};
  m_lanes.push_back(GalaxyLane{.a = low, .b = high, .costTicks = _costTicks});

  // Appended in lane order to both endpoints, so `LanesAt` comes back ascending without a sort.
  m_lanesAt[low.AsSize()].push_back(id);
  m_lanesAt[high.AsSize()].push_back(id);
  return id;
}

const std::vector<LaneId>& Galaxy::LanesAt(SystemId _system) const
{
  return m_lanesAt[_system.AsSize()];
}

SystemId Galaxy::OtherEnd(LaneId _lane, SystemId _from) const
{
  const GalaxyLane& lane = m_lanes[_lane.AsSize()];
  if (lane.a == _from)
  {
    return lane.b;
  }
  if (lane.b == _from)
  {
    return lane.a;
  }
  return SystemId{};
}

std::vector<std::uint32_t> Galaxy::ShortestPathTicksFrom(SystemId _from) const
{
  std::vector<std::uint32_t> best(m_systems.size(), UNREACHABLE);
  if (!_from.IsValid())
  {
    return best;
  }

  // Dijkstra. A lane cost is time and the costs differ, so a breadth-first walk would answer the
  // wrong question -- it would find the path with fewest lanes, and the one-pager's distances are
  // in ticks (one-pager, "Shape of a game": distance is authored).
  //
  // The frontier is ordered by (cost, system) rather than by cost alone. The tiebreak is not
  // decoration: two systems at the same cost must be visited in the same order on every machine or
  // the generator's accept/reject decision could differ between them (ADR-018).
  using Entry = std::pair<std::uint32_t, SystemId>;
  const auto worseFirst = [](const Entry& _left, const Entry& _right)
  {
    if (_left.first != _right.first)
    {
      return _left.first > _right.first;
    }
    return _left.second > _right.second;
  };
  std::priority_queue<Entry, std::vector<Entry>, decltype(worseFirst)> frontier{worseFirst};

  best[_from.AsSize()] = 0;
  frontier.emplace(0U, _from);

  while (!frontier.empty())
  {
    const auto [cost, at] = frontier.top();
    frontier.pop();
    if (cost > best[at.AsSize()])
    {
      continue;
    }

    for (const LaneId lane : m_lanesAt[at.AsSize()])
    {
      const SystemId other = OtherEnd(lane, at);
      const std::uint32_t through = cost + m_lanes[lane.AsSize()].costTicks;
      if (through < best[other.AsSize()])
      {
        best[other.AsSize()] = through;
        frontier.emplace(through, other);
      }
    }
  }

  return best;
}

std::uint32_t Galaxy::ShortestPathTicks(SystemId _from, SystemId _to) const
{
  if (!_from.IsValid() || !_to.IsValid())
  {
    return UNREACHABLE;
  }
  return ShortestPathTicksFrom(_from)[_to.AsSize()];
}

bool Galaxy::IsConnected() const
{
  if (m_systems.empty())
  {
    return true;
  }

  const std::vector<std::uint32_t> reach = ShortestPathTicksFrom(SystemId{0});
  for (const std::uint32_t ticks : reach)
  {
    if (ticks == UNREACHABLE)
    {
      return false;
    }
  }
  return true;
}

std::vector<SystemId> Galaxy::Capitals() const
{
  std::vector<SystemId> capitals;
  for (std::size_t index = 0; index < m_systems.size(); ++index)
  {
    if (m_systems[index].kind == SystemKind::Capital)
    {
      capitals.emplace_back(static_cast<Neuron::Id<SystemTag>::Underlying>(index));
    }
  }
  return capitals;
}

} // namespace Frontier
