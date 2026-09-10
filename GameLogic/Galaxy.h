#pragma once

#include "Id.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

// The identifiers the game names things with. `Neuron::Id` is the engine's typed index (R9: the
// template knows nothing about this game); the tags below are the game's, and they live beside the
// arrays they index.
struct PlayerTag;
struct SystemTag;
struct LaneTag;

using PlayerId = Neuron::Id<PlayerTag>;
using SystemId = Neuron::Id<SystemTag>;
using LaneId = Neuron::Id<LaneTag>;

/// What a system is FOR, at generation time.
///
/// It is not a game rule -- nothing resolves differently because a system is a satellite rather
/// than a frontier world -- but it is what the generator's own constraints are written against, and
/// it is what lets `Validate` check them from the finished graph rather than from the generator's
/// working notes. It is also the honest place for the region's anchor, which is a position and a
/// set of lanes and is not a system anybody can own.
enum class SystemKind : std::uint8_t
{
  /// A player's starting seat. One per player, guarded for the first ticks of the match.
  Capital,
  /// A starting-cluster world, one tick from its capital.
  Satellite,
  /// A world on a cluster boundary. The one-pager's first contact happens here.
  Border,
  /// The contested middle, two to four ticks from anywhere.
  Frontier,
  /// The sealed region's centre. Carries lanes, is drawn as a region rather than a node, and
  /// cannot be owned (one-pager: "Empires can raid it, not claim it").
  RegionAnchor
};

/// A node of the galaxy graph.
///
/// `positionX/Y` are integers in the 800x560 design space the client's map is authored in, and they
/// exist ONLY to be drawn. Nothing in the simulation reads them: the one-pager is explicit that
/// distance is authored as a lane cost rather than emergent from coordinates ("Not a coordinate
/// map -- there is no velocity to tune"). They are integers because `GameLogic` is integer
/// arithmetic end to end (R16, ADR-018), and the drawing side converts.
struct GalaxySystem
{
  std::string name;
  SystemKind kind = SystemKind::Frontier;
  /// Who holds it now. Invalid means unowned -- which at generation is everything but the
  /// capitals.
  PlayerId owner;
  /// Whose starting cluster it was generated into, or invalid for the frontier and the region.
  /// Kept because it is what `Validate` checks the one-tick-inside-a-cluster rule against, and
  /// because "where did this player start" outlives generation.
  PlayerId startingCluster;
  std::int32_t positionX = 0;
  std::int32_t positionY = 0;
};

/// An edge. Undirected: `a` and `b` are stored in ascending index order so that a lane between two
/// systems has one representation and duplicate detection is a comparison rather than a search.
struct GalaxyLane
{
  SystemId a;
  SystemId b;
  /// Ticks to traverse. Authored at generation, never derived from the positions above.
  std::uint32_t costTicks = 1;
};

/// The bounded galaxy a match is played on.
///
/// It is a graph and nothing more: no ships, no production, no orders. Those belong to the match
/// state that Step 3 builds around it, and keeping them out is what lets the generator and its
/// constraints be tested on their own.
class Galaxy
{
public:
  [[nodiscard]] const std::vector<GalaxySystem>& Systems() const noexcept
  {
    return m_systems;
  }
  [[nodiscard]] const std::vector<GalaxyLane>& Lanes() const noexcept
  {
    return m_lanes;
  }

  [[nodiscard]] const GalaxySystem& SystemAt(SystemId _system) const
  {
    return m_systems[_system.AsSize()];
  }
  [[nodiscard]] const GalaxyLane& LaneAt(LaneId _lane) const
  {
    return m_lanes[_lane.AsSize()];
  }

  [[nodiscard]] std::uint32_t SystemCount() const noexcept
  {
    return static_cast<std::uint32_t>(m_systems.size());
  }
  [[nodiscard]] std::uint32_t LaneCount() const noexcept
  {
    return static_cast<std::uint32_t>(m_lanes.size());
  }

  /// The region's centre, or an invalid id if the galaxy has none.
  [[nodiscard]] SystemId RegionAnchor() const noexcept
  {
    return m_regionAnchor;
  }

  [[nodiscard]] SystemId AddSystem(GalaxySystem _system);

  /// Adds an undirected lane, normalising the endpoint order. Returns an invalid id when the lane
  /// would be a self-loop or a duplicate -- both of which are generator bugs rather than states to
  /// be tolerated, so the caller checks.
  [[nodiscard]] LaneId AddLane(SystemId _a, SystemId _b, std::uint32_t _costTicks);

  /// The lanes touching a system, in ascending lane order. Ascending because ADR-018 requires that
  /// anything iterated into a result has a defined order, and this is iterated by every path
  /// search in the game.
  [[nodiscard]] const std::vector<LaneId>& LanesAt(SystemId _system) const;

  /// The other end of a lane. Returns an invalid id if `_from` is not on it.
  [[nodiscard]] SystemId OtherEnd(LaneId _lane, SystemId _from) const;

  /// Shortest path in TICKS, by Dijkstra over lane costs.
  ///
  /// Ticks, not hops: a lane's cost is time, and the one-pager's "a rival capital within three
  /// ticks" is a statement about time. Returns `UNREACHABLE` when no path exists.
  [[nodiscard]] std::uint32_t ShortestPathTicks(SystemId _from, SystemId _to) const;

  /// Shortest path in ticks from one system to every other, in one sweep. What the generator's
  /// capital-distance check uses, because doing it pairwise would be the same work N times over.
  [[nodiscard]] std::vector<std::uint32_t> ShortestPathTicksFrom(SystemId _from) const;

  [[nodiscard]] bool IsConnected() const;

  /// Every capital, in ascending system order.
  [[nodiscard]] std::vector<SystemId> Capitals() const;

  static constexpr std::uint32_t UNREACHABLE = 0xFFFFFFFFU;

private:
  std::vector<GalaxySystem> m_systems;
  std::vector<GalaxyLane> m_lanes;
  /// Parallel to `m_systems`. Rebuilt as lanes are added rather than on demand, so that a path
  /// search never mutates the graph it is reading.
  std::vector<std::vector<LaneId>> m_lanesAt;
  SystemId m_regionAnchor;
};

} // namespace Frontier
