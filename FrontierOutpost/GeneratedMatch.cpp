// GeneratedMatch.cpp -- a generated galaxy, in the shape the main page reads.
//
// Two vocabularies meet here and nowhere else. On one side `GameLogic`'s `Galaxy`: typed ids,
// integer positions, authored lane costs, no idea what a colour is. On the other `MatchState`: the
// client's view model, floats because it is about to be projected, an `Owner` that doubles as a
// palette entry. The conversion is small and it is deliberately all in one file, because a seam
// spread over several is a seam nobody can see.

#include "pch.h"
#include "GeneratedMatch.h"

#include "GalaxyGenerator.h"

#include <cmath>
#include <numbers>

namespace Frontier
{

namespace
{

/// Seconds in a day, for turning a tick interval into the day count the top bar shows.
constexpr double SECONDS_PER_DAY = 24.0 * 60.0 * 60.0;

/// How far the region's settlement sites sit from its anchor, in design-space units.
constexpr float REGION_SITE_RADIUS = 26.0F;

/// Which colour a player is drawn in.
///
/// **This is a placeholder and it is lossy.** `Owner` has three players and a neutral because it
/// was written for the design reference, which shows you, Halvorsen and Sorne; a generated galaxy
/// has six to twelve. So player 0 is you and the rest alternate between the two rival colours,
/// which at least gives every capital a neighbour it does not match.
///
/// Widening the client's owner model is step 8 of `Design/Plans/4X-01-CoreLoop.md`, and it is a
/// design question rather than a mechanical one: twelve distinguishable colours at these sizes,
/// against a token palette that already spends its hues on meaning (loss, contact, the region), is
/// not something to settle by picking eight more constants here.
[[nodiscard]] Owner OwnerOf(PlayerId _player) noexcept
{
  if (!_player.IsValid())
  {
    return Owner::Neutral;
  }
  if (_player.Index() == 0)
  {
    return Owner::You;
  }
  return (_player.Index() % 2) == 1 ? Owner::Halvorsen : Owner::Sorne;
}

[[nodiscard]] SystemFlags FlagsOf(SystemKind _kind) noexcept
{
  switch (_kind)
  {
  case SystemKind::Capital:
    return SystemFlags::Capital;
  case SystemKind::RegionAnchor:
    return SystemFlags::RegionAnchor;
  case SystemKind::Satellite:
  case SystemKind::Border:
  case SystemKind::Frontier:
  default:
    return SystemFlags::None;
  }
}

/// The galaxy graph, converted node for node and lane for lane.
///
/// Indices survive the conversion unchanged -- system `n` in the `Galaxy` is system `n` in the
/// `Graph` -- which is what lets everything else in this file refer to a system by the id the
/// generator gave it. Positions widen from integer to float here and nowhere else: they are
/// integers in `GameLogic` because the simulation is integer end to end (ADR-018), and floats on
/// this side because the next thing that happens to them is a projection.
[[nodiscard]] Graph ConvertGraph(const Galaxy& _galaxy)
{
  Graph graph;
  graph.systems.reserve(_galaxy.Systems().size());
  graph.lanes.reserve(_galaxy.Lanes().size());

  for (const GalaxySystem& system : _galaxy.Systems())
  {
    graph.systems.push_back(SystemNode{
      .name = system.name,
      .owner = OwnerOf(system.owner),
      .positionX = static_cast<float>(system.positionX),
      .positionY = static_cast<float>(system.positionY),
      .flags = FlagsOf(system.kind),
    });
  }

  for (const GalaxyLane& lane : _galaxy.Lanes())
  {
    // `LaneKind` is a diplomatic fact, not a graph fact (MatchState.h): a generated galaxy has no
    // agreements in it yet, so every lane is plain.
    graph.lanes.push_back(Lane{
      .a = lane.a.Index(),
      .b = lane.b.Index(),
      .cost = lane.costTicks,
      .kind = LaneKind::None,
    });
  }

  return graph;
}

} // namespace

MatchState MakeGeneratedMatch(const MatchRules& _rules, std::uint64_t _seed)
{
  const GeneratedGalaxy generated = GalaxyGenerator::Generate(_rules, _seed);

  MatchState state;
  state.graph = ConvertGraph(generated.galaxy);

  // Four ticks a day at a six-hour interval, but derived rather than assumed: Phase 0 runs an
  // hour-long tick, and a top bar reading "DAY 12 OF 21" under a one-hour tick would be wrong in a
  // way nobody would look for.
  const double ticksPerDay = SECONDS_PER_DAY / static_cast<double>(_rules.tickIntervalSeconds);
  const auto days = static_cast<std::uint32_t>(std::lround(static_cast<double>(_rules.matchLengthTicks) / ticksPerDay));

  state.match = Match{
    .id = std::format("{:04X}", static_cast<std::uint16_t>(generated.seed & 0xFFFFULL)),
    .day = 1,
    .totalDays = days,
    // The server owns the schedule and there is no server (4X-02), so there is no end time to
    // report. An invented one would be the first wall-clock lie on this screen.
    .endsAt = "",
    .tick = 0,
    .secondsToLock = static_cast<double>(_rules.tickIntervalSeconds),
  };

  // Nobody has scored, so everybody is level and there is no leader to name.
  state.player = PlayerStanding{
    .score = 0,
    .placement = 1,
    .playerCount = _rules.playerCount,
    .leader = Leader{},
  };

  state.totalSystems = generated.galaxy.SystemCount();

  std::uint32_t unclaimed = 0;
  for (const GalaxySystem& system : generated.galaxy.Systems())
  {
    // The region is not unclaimed, it is unclaimable -- raided, not owned (one-pager).
    if (!system.owner.IsValid() && system.kind != SystemKind::RegionAnchor)
    {
      ++unclaimed;
    }
  }
  state.unclaimedSystems = unclaimed;

  const SystemId anchor = generated.galaxy.RegionAnchor();
  state.region = Region{
    .anchor = anchor.IsValid() ? anchor.Index() : EventRefs::NONE,
    .opensAt = _rules.regionOpensAtTick,
  };

  // Sites are illustrative geometry rather than simulation state (Design/Screens/README.md
  // "Fidelity"), so they are laid out here in floats rather than asked of `GameLogic` -- there is
  // nothing in the rules yet that says where inside the region a settlement goes.
  for (std::uint32_t site = 0; site < _rules.regionSiteCount; ++site)
  {
    const double angle = (2.0 * std::numbers::pi * site) / static_cast<double>(_rules.regionSiteCount);
    state.region.siteOffsets.emplace_back(static_cast<float>(std::cos(angle)) * REGION_SITE_RADIUS,
                                          static_cast<float>(std::sin(angle)) * REGION_SITE_RADIUS);
  }

  // `digest`, `fleets`, `proposals` and `orders` are left empty on purpose. See GeneratedMatch.h.
  return state;
}

} // namespace Frontier
