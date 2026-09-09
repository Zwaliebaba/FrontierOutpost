#pragma once

#include <cstdint>
#include <limits>

namespace Neuron
{

/// An angle, 65536 units to a full turn.
///
/// Not radians and not degrees, and both of those matter. It is exact: a full turn is a whole
/// number of units, so a heading wraps by unsigned overflow with no accumulated error and no
/// normalization step that could be forgotten. And it is integral, which is what the simulation
/// needs -- R16 keeps float out of GameLogic, and an angle is the quantity most likely to smuggle
/// one in.
using Turns16 = std::uint16_t;

/// A full turn, as the arithmetic type the maths below is done in. It does not fit in a Turns16,
/// which is the point: every representable Turns16 is a distinct heading.
inline constexpr std::int32_t TURNS16_PER_TURN = 65536;
inline constexpr std::int32_t TURNS16_PER_QUARTER_TURN = 16384;

/// 1.0, in the fixed point SineCosine returns. Sixteen fractional bits.
inline constexpr std::int32_t TRIG_ONE = 65536;

struct SineCosine
{
  /// Both scaled by TRIG_ONE, so they run over [-65536, 65536].
  std::int32_t sine;
  std::int32_t cosine;
};

/// Sine and cosine of a heading, computed by CORDIC in integers only.
///
/// Measured against the exact values at 9363 headings across the full turn: worst error 19 of
/// 65536, which is 0.03% (measured 2026-09-09, and asserted by NeuronCoreTests so the figure and
/// the code cannot drift apart). At the ship's top speed of 1200 mm a tick that is an error of
/// under half a millimetre a tick, against a simulation whose unit is the millimetre.
///
/// The four cardinal directions are EXACT rather than merely close, because heading zero meaning
/// exactly +X is something the rest of the game reasons with.
[[nodiscard]] SineCosine SineCosineTurns16(Turns16 _angle) noexcept;

/// The heading of the direction (_x, _z), by CORDIC vectoring.
///
/// The argument order is (x, z) and not the (y, x) that atan2 takes, because this game's ground
/// plane is X and Z and heading zero is +X turning towards +Z.
///
/// Worst error measured over 3600 directions at three magnitudes spanning nine orders of
/// magnitude: 9 units, which is 0.049 degrees (2026-09-09). The axes are exact at every
/// magnitude.
///
/// A zero vector has no direction; it returns heading zero rather than pretending otherwise, and
/// callers that care must check the distance first.
[[nodiscard]] Turns16 Atan2Turns16(std::int64_t _x, std::int64_t _z) noexcept;

/// The shortest way round from one heading to another, in [-32768, 32767]. Positive turns towards
/// +Z. This is the function that makes turning "the short way" free: the arithmetic wraps, so
/// there is no case analysis about crossing zero.
[[nodiscard]] constexpr std::int32_t ShortestTurnTurns16(Turns16 _from, Turns16 _to) noexcept
{
  const auto difference = static_cast<std::uint16_t>(static_cast<std::uint16_t>(_to) - static_cast<std::uint16_t>(_from));
  return static_cast<std::int32_t>(static_cast<std::int16_t>(difference));
}

/// The integer square root: the largest n with n*n <= _value. Exact, and no floating point
/// anywhere near it -- std::sqrt on a large integer is a double rounding away from a different
/// answer on a different machine, which is the whole of what R16 is about.
[[nodiscard]] std::uint64_t IntegerSquareRoot(std::uint64_t _value) noexcept;

/// Addition that stops at the ends of the range instead of overflowing.
///
/// Signed overflow is undefined behavior, so "it would take ten billion years to reach" is not an
/// argument for leaving it out -- an undefined program is undefined today. This costs a compare.
[[nodiscard]] constexpr std::int64_t SaturatingAdd(std::int64_t _a, std::int64_t _b) noexcept
{
  if (_b > 0 && _a > std::numeric_limits<std::int64_t>::max() - _b)
  {
    return std::numeric_limits<std::int64_t>::max();
  }
  if (_b < 0 && _a < std::numeric_limits<std::int64_t>::min() - _b)
  {
    return std::numeric_limits<std::int64_t>::min();
  }
  return _a + _b;
}

} // namespace Neuron
