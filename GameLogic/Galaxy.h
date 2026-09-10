#pragma once

#include "MatchState.h"
#include "Rules.h"

#include <cstdint>
#include <string>

namespace Frontier
{

/// Why a seed was refused, or that it was accepted.
///
/// Rejection is a named result rather than a retry loop inside the generator, because a rejection
/// rate is a figure ADR-014 asks to be measured and a loop hides it. `GenerateGalaxyWithRetries`
/// is the loop, and it is separate.
enum class GenerationResult : std::uint8_t
{
  Accepted,
  /// Two systems ended up closer than `minSeparationUnits`.
  SeparationTooSmall,
  /// A lane came out longer than the last cost band, so it has no cost to carry.
  LaneTooLong,
  /// A lane inside a starting cluster did not come out at one tick.
  ClusterLaneNotOneTick,
  /// A lane outside a starting cluster came out under two ticks.
  FrontierLaneUnderTwoTicks,
  /// Two lanes exist where the longer one costs less, which would let the map lie (ADR-014).
  LengthNotMonotoneInCost,
  /// Two lanes cross, which the eye reads as a junction that is not there.
  LanesCross,
  /// Some system cannot be reached from a capital.
  Disconnected,
  /// Some capital has no rival capital within `capitalRivalMaxTicks`.
  NoRivalCapitalWithinReach
};

[[nodiscard]] const char* Describe(GenerationResult _result) noexcept;

/// Builds a galaxy for `_seatCount` seats from `_seed`, or says why that seed will not do.
///
/// On anything but `Accepted`, `_outState` holds the galaxy that failed -- the tests want to look
/// at it, and a caller that is retrying is going to overwrite it anyway.
///
/// The shape is concentric and planar by construction (ADR-014): an outer ring of capitals each
/// with an outward arc of cluster systems, a chord between adjacent capitals, a frontier ring one
/// radial spoke inward with a ring cycle, and the sealed region as a small ring at the center
/// spoked out to the frontier. Lane costs are then read off each lane's drawn length, so
/// monotonicity holds by construction and the checks below are a safety net rather than a search.
[[nodiscard]] GenerationResult GenerateGalaxy(std::uint64_t _seed, SeatId _seatCount, const Rules& _rules, MatchState& _outState);

/// Tries consecutive seeds from `_seed` until one is accepted. Returns the number of seeds tried,
/// or 0 when `_attempts` were all refused.
[[nodiscard]] std::uint32_t GenerateGalaxyWithRetries(std::uint64_t _seed, SeatId _seatCount, const Rules& _rules, std::uint32_t _attempts,
                                                      MatchState& _outState);

/// The galaxy as text, for a person to read in a test log. Not a wire format and not parsed by
/// anything: the map has no picture until slice 2, and a generator nobody can look at is a
/// generator whose bugs are found by a renderer.
[[nodiscard]] std::string DescribeGalaxy(const MatchState& _state);

} // namespace Frontier
