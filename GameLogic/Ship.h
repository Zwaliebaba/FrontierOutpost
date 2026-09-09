#pragma once

#include "Trigonometry.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Frontier
{

/// One ship, and everything that happens to it in a tick (ADR-004).
///
/// Every quantity is an integer and every one carries its unit in its name (R6, R16). There is no
/// float anywhere in this class and no wall clock: the tick is the clock, so a Ship that has been
/// ticked the same number of times from the same start is in the same place, to the millimetre,
/// on any machine.
class Ship
{
public:
  /// Millimetres. R6 puts the unit in the name because the renderer works in metres and the
  /// screen works in pixels, and a number that could be any of the three is a defect waiting.
  ///
  /// A std::int64_t of millimetres reaches +/- 9.22e18 mm, which is about 0.97 light-years. At
  /// MAX_SPEED it would take 2.4e10 years to get there, so the end of the range is not somewhere
  /// the simulation goes -- but the addition saturates anyway (Neuron::SaturatingAdd), because
  /// signed overflow is undefined today and "unreachable" is not a defence against undefined.
  static constexpr std::int32_t MAX_SPEED_MILLIMETRES_PER_TICK = 1200;

  /// 60 mm/tick added per tick. At 20 Hz that is 1.2 m/s per tick, so the ship reaches its top
  /// speed of 24 m/s in twenty ticks -- one second.
  static constexpr std::int32_t ACCELERATION_MILLIMETRES_PER_TICK_SQUARED = 60;

  /// 1024 of 65536 is a sixteenth of a turn a tick: 5.625 degrees, or 112.5 degrees a second. A
  /// half turn takes 1.6 seconds, which is slow enough to read as a ship turning rather than a
  /// sprite snapping round.
  static constexpr std::int32_t TURN_RATE_TURNS16_PER_TICK = 1024;

  /// The ship does not accelerate while it is pointing more than this far from where it is going
  /// -- an eighth of a turn, 45 degrees. Without it a ship ordered to somewhere behind it flies a
  /// wide arc away from the target before coming back; with it, it slows, turns, and then goes.
  static constexpr std::int32_t FACING_TOLERANCE_TURNS16 = 8192;

  void OrderMoveTo(std::int64_t _targetXMillimetres, std::int64_t _targetZMillimetres) noexcept;

  /// One tick: turn, then choose a speed, then move.
  void Tick() noexcept;

  [[nodiscard]] std::int64_t PositionXMillimetres() const noexcept
  {
    return m_positionXMillimetres;
  }
  [[nodiscard]] std::int64_t PositionZMillimetres() const noexcept
  {
    return m_positionZMillimetres;
  }
  [[nodiscard]] Neuron::Turns16 HeadingTurns16() const noexcept
  {
    return m_headingTurns16;
  }
  [[nodiscard]] std::int32_t SpeedMillimetresPerTick() const noexcept
  {
    return m_speedMillimetresPerTick;
  }
  [[nodiscard]] bool HasOrder() const noexcept
  {
    return m_hasOrder;
  }

  /// How far the ship would travel if it started braking now. The arrival test is a comparison
  /// against this, which is what makes the ship stop ON the target rather than near it.
  ///
  /// Exposed because it is the whole of the kinematics that is worth testing in isolation: it is
  /// a closed form, and the obvious implementation -- run the deceleration in a loop and add it
  /// up -- is what the test compares it against.
  [[nodiscard]] static std::int64_t StoppingDistanceMillimetres(std::int32_t _speedMillimetresPerTick) noexcept;

  /// Distance to a point, saturating at INT64_MAX when the components are too large to square.
  /// Anything that far away is further than any stopping distance, so the saturated value is
  /// still the right answer to every question the kinematics asks of it.
  [[nodiscard]] static std::int64_t DistanceMillimetres(std::int64_t _deltaX, std::int64_t _deltaZ) noexcept;

private:
  std::int64_t m_positionXMillimetres = 0;
  std::int64_t m_positionZMillimetres = 0;
  std::int64_t m_targetXMillimetres = 0;
  std::int64_t m_targetZMillimetres = 0;
  std::int32_t m_speedMillimetresPerTick = 0;
  Neuron::Turns16 m_headingTurns16 = 0;
  bool m_hasOrder = false;
};

} // namespace Frontier
