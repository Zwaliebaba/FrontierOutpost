#pragma once

#include "Galaxy.h"
#include "MatchRules.h"

#include <cstdint>

namespace Frontier
{

/// Why a candidate galaxy was refused.
///
/// The one-pager says "a seed that can't satisfy this is rejected", which makes generation a search
/// -- and a search whose failures are anonymous is one nobody can debug. Each value below names a
/// constraint from the one-pager, so a run of rejections says which rule the parameters are
/// fighting rather than that something went wrong.
enum class GalaxyRejection : std::uint8_t
{
  None,
  /// Player count outside the 6-12 the design is drawn for.
  PlayerCountOutOfRange,
  /// Some system cannot be reached from some other. A bounded galaxy is one galaxy.
  Disconnected,
  /// A capital has no rival capital within `maximumTicksToNearestRival`.
  RivalTooFar,
  /// A lane inside one starting cluster does not cost one tick.
  ClusterLaneNotOneTick,
  /// A lane leaving a cluster is outside the two-to-four band.
  FrontierLaneOutOfBand,
  /// The sealed region was not placed, or was placed without lanes reaching it.
  RegionUnreachable
};

[[nodiscard]] const char* Describe(GalaxyRejection _rejection) noexcept;

struct GeneratedGalaxy
{
  Galaxy galaxy;
  /// The seed that produced it -- the one the match is played on and the one a bug report quotes.
  std::uint64_t seed = 0;
  /// How many seeds were refused before this one. Zero nearly always; a number that creeps up is
  /// the generator telling you the parameters have got tight.
  std::uint32_t rejectedSeeds = 0;
};

/// Builds the bounded galaxy a match is played on.
///
/// The shape is the one-pager's, and it is a ring: capitals evenly spaced around the edge, each
/// with a starting cluster of satellites a tick away, a border system between each pair of
/// neighbours, a contested frontier inside the ring, and the sealed region at the centre.
///
/// THE RING IS THE FAIRNESS ARGUMENT. Every capital has exactly two neighbours at the same
/// distance, and the region is equidistant from all of them. The test plan's Phase 2 asks that "no
/// empire is consistently positioned to dominate the region at opening", and a layout where
/// somebody starts nearest is a layout that fails that before anyone plays. It also gives the
/// one-pager's "dense start, sparse frontier" for free: neighbours are three ticks apart and the
/// middle is further from everyone.
///
/// It is a PURE FUNCTION of (rules, seed) -- ADR-018 -- so the same inputs give the same galaxy on
/// every machine, which is what makes a match the same match for everyone playing it.
class GalaxyGenerator
{
public:
  /// One attempt. Returns `GalaxyRejection::None` and fills `_outGalaxy` on success, or the reason
  /// it refused.
  [[nodiscard]] static GalaxyRejection TryGenerate(const MatchRules& _rules, std::uint64_t _seed, Galaxy& _outGalaxy);

  /// Attempts seeds derived from `_firstSeed` until one is accepted.
  ///
  /// Fails loudly rather than returning a galaxy that breaks a rule: if `maximumSeedAttempts` are
  /// all refused, the parameters cannot be satisfied and that is a bug in the rules or in this
  /// file, not a run of bad luck. Debug.h's Fatal is the right answer to a broken invariant.
  [[nodiscard]] static GeneratedGalaxy Generate(const MatchRules& _rules, std::uint64_t _firstSeed);

  /// Checks a finished galaxy against every constraint the one-pager states.
  ///
  /// Separate from generation, and reading only the graph, so that it is a test of the RULES rather
  /// than a re-run of the code that built it. A generator that validated using its own working
  /// notes would agree with itself about a mistake.
  [[nodiscard]] static GalaxyRejection Validate(const Galaxy& _galaxy, const MatchRules& _rules);

  /// The design space the client draws in, and the two rings things sit on. Positions are for
  /// drawing only (Galaxy.h).
  ///
  /// The rings are ELLIPSES rather than circles because the design space is wider than it is tall,
  /// and a circle inside it wastes the sides. Nothing in the simulation notices -- a lane's cost is
  /// authored, not measured (one-pager) -- so the shape is purely a question of using the pane.
  static constexpr std::int32_t DESIGN_WIDTH = 800;
  static constexpr std::int32_t DESIGN_HEIGHT = 560;
  static constexpr std::int32_t CENTER_X = DESIGN_WIDTH / 2;
  static constexpr std::int32_t CENTER_Y = DESIGN_HEIGHT / 2;
  static constexpr std::int32_t CAPITAL_RING_RADIUS_X = 300;
  static constexpr std::int32_t CAPITAL_RING_RADIUS_Y = 200;
  static constexpr std::int32_t FRONTIER_RING_RADIUS_X = 140;
  static constexpr std::int32_t FRONTIER_RING_RADIUS_Y = 95;
  /// How far a satellite sits from its capital, and how far any position may be nudged. Jitter is
  /// what stops twelve matches looking like the same picture rotated.
  static constexpr std::int32_t SATELLITE_RADIUS = 52;
  static constexpr std::int32_t POSITION_JITTER = 16;
};

} // namespace Frontier
