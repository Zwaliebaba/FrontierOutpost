#pragma once

#include <cstdint>

namespace Neuron
{

/// An angle as a fraction of a turn, in 65536ths.
///
/// A whole turn wraps to zero by the type's own arithmetic, which is the property that makes this
/// worth having over degrees or radians: there is no normalisation step to forget and no angle that
/// is out of range.
using Turns16 = std::uint16_t;

inline constexpr Turns16 QUARTER_TURN = 16384;
inline constexpr Turns16 HALF_TURN = 32768;

/// What `Sine` and `Cosine` are scaled by. Integer, because `GameLogic` is integer end to end
/// (R16) -- and because a galaxy laid out with `std::cos` would be laid out DIFFERENTLY on a
/// different standard library, which is precisely what ADR-018 exists to prevent. `std::cos` is
/// not required to be correctly rounded and implementations disagree in the last bits.
inline constexpr std::int32_t TRIG_SCALE = 1024;

/// Sine, scaled by TRIG_SCALE, in integer arithmetic only.
///
/// Bhaskara I's approximation, which is a ratio of two quadratics and needs no table:
///
///     sin(pi * u) ~= 16u(1 - u) / (5 - 4u(1 - u))     for u in [0, 1]
///
/// Its worst absolute error is about 0.0016, which at the radii a galaxy is laid out on is well
/// under half a pixel. It is here to place things on a ring, not to do physics.
[[nodiscard]] constexpr std::int32_t Sine(Turns16 _angle) noexcept
{
  // The second half of a turn is the first half, negated.
  const bool negative = _angle >= HALF_TURN;
  const std::int64_t half = negative ? _angle - HALF_TURN : _angle;

  // p = u(1 - u) with u = half / 32768, kept scaled: p * 32768^2.
  const std::int64_t scaledP = half * (static_cast<std::int64_t>(HALF_TURN) - half);

  constexpr std::int64_t UNIT = static_cast<std::int64_t>(HALF_TURN) * HALF_TURN;
  const std::int64_t numerator = 16 * scaledP * TRIG_SCALE;
  const std::int64_t denominator = 5 * UNIT - 4 * scaledP;

  const auto magnitude = static_cast<std::int32_t>(numerator / denominator);
  return negative ? -magnitude : magnitude;
}

[[nodiscard]] constexpr std::int32_t Cosine(Turns16 _angle) noexcept
{
  // cos(x) = sin(x + quarter turn), and the wrap is the type's.
  return Sine(static_cast<Turns16>(_angle + QUARTER_TURN));
}

/// `_radius` units along `_angle`, rounded to the nearest whole unit.
///
/// Rounds half away from zero rather than truncating, so that a ring of points is symmetric about
/// its centre instead of drifting one unit toward it on the negative side.
[[nodiscard]] constexpr std::int32_t OffsetAlong(std::int32_t _radius, std::int32_t _trigValue) noexcept
{
  const std::int64_t scaled = static_cast<std::int64_t>(_radius) * _trigValue;
  const std::int64_t rounded = scaled >= 0 ? scaled + TRIG_SCALE / 2 : scaled - TRIG_SCALE / 2;
  return static_cast<std::int32_t>(rounded / TRIG_SCALE);
}

/// The angle `_step` of `_steps` evenly spaced around a turn.
[[nodiscard]] constexpr Turns16 EvenlySpaced(std::uint32_t _step, std::uint32_t _steps) noexcept
{
  if (_steps == 0)
  {
    return 0;
  }
  return static_cast<Turns16>((static_cast<std::uint64_t>(_step) * 65536U) / _steps);
}

} // namespace Neuron
