// Ship.cpp -- turn-rate-limited heading, acceleration to a top speed, and a deceleration that
// arrives stopped. ADR-004 is the decision; this is the arithmetic.

#include "pch.h"
#include "Ship.h"

namespace Frontier
{

namespace
{

/// Beyond this, squaring a component overflows a signed 64-bit product. 2^31 mm is 2147 km, and
/// two components that large are already further away than any stopping distance.
constexpr std::int64_t SQUARABLE_LIMIT_MILLIMETRES = 1LL << 31;

} // namespace

std::int64_t Ship::DistanceMillimetres(std::int64_t _deltaX, std::int64_t _deltaZ) noexcept
{
  const std::int64_t absoluteX = _deltaX < 0 ? -_deltaX : _deltaX;
  const std::int64_t absoluteZ = _deltaZ < 0 ? -_deltaZ : _deltaZ;

  if (absoluteX >= SQUARABLE_LIMIT_MILLIMETRES || absoluteZ >= SQUARABLE_LIMIT_MILLIMETRES)
  {
    return std::numeric_limits<std::int64_t>::max();
  }

  const auto squared = static_cast<std::uint64_t>(absoluteX * absoluteX) + static_cast<std::uint64_t>(absoluteZ * absoluteZ);
  return static_cast<std::int64_t>(Neuron::IntegerSquareRoot(squared));
}

std::int64_t Ship::StoppingDistanceMillimetres(std::int32_t _speedMillimetresPerTick) noexcept
{
  if (_speedMillimetresPerTick <= 0)
  {
    return 0;
  }

  // Tick order is: choose a speed, then move at it. So braking from v covers
  //   (v - a) + (v - 2a) + ... down to the last positive step,
  // which is n - 1 terms where n is the number of ticks to reach zero. The closed form below is
  // that sum; GameLogicTests checks it against the loop it replaces, at every speed the ship can
  // actually be at.
  const std::int64_t speed = _speedMillimetresPerTick;
  const std::int64_t acceleration = ACCELERATION_MILLIMETRES_PER_TICK_SQUARED;
  const std::int64_t ticksToStop = (speed + acceleration - 1) / acceleration;

  return (ticksToStop - 1) * speed - acceleration * (ticksToStop - 1) * ticksToStop / 2;
}

void Ship::OrderMoveTo(std::int64_t _targetXMillimetres, std::int64_t _targetZMillimetres) noexcept
{
  m_targetXMillimetres = _targetXMillimetres;
  m_targetZMillimetres = _targetZMillimetres;
  m_hasOrder = true;
}

void Ship::Tick() noexcept
{
  // With no order the ship coasts to a stop on its current heading rather than stopping dead.
  // A ship that halts the instant its order is cancelled reads as a bug even when it is not.
  if (!m_hasOrder)
  {
    m_speedMillimetresPerTick = std::max(0, m_speedMillimetresPerTick - ACCELERATION_MILLIMETRES_PER_TICK_SQUARED);
  }
  else
  {
    const std::int64_t deltaX = m_targetXMillimetres - m_positionXMillimetres;
    const std::int64_t deltaZ = m_targetZMillimetres - m_positionZMillimetres;
    const std::int64_t distance = DistanceMillimetres(deltaX, deltaZ);

    // Turn first, by at most the turn rate, the short way round.
    const Neuron::Turns16 desiredHeading = Neuron::Atan2Turns16(deltaX, deltaZ);
    const std::int32_t wanted = Neuron::ShortestTurnTurns16(m_headingTurns16, desiredHeading);
    const std::int32_t turn = std::clamp(wanted, -TURN_RATE_TURNS16_PER_TICK, TURN_RATE_TURNS16_PER_TICK);
    m_headingTurns16 = static_cast<Neuron::Turns16>(static_cast<std::uint16_t>(m_headingTurns16) + static_cast<std::uint16_t>(turn));

    // Then choose a speed for the heading we now have.
    const std::int32_t remainingError = Neuron::ShortestTurnTurns16(m_headingTurns16, desiredHeading);
    const std::int32_t absoluteError = remainingError < 0 ? -remainingError : remainingError;
    const bool facingTheTarget = absoluteError <= FACING_TOLERANCE_TURNS16;
    const bool timeToBrake = distance <= StoppingDistanceMillimetres(m_speedMillimetresPerTick);

    if (!facingTheTarget || timeToBrake)
    {
      m_speedMillimetresPerTick = std::max(0, m_speedMillimetresPerTick - ACCELERATION_MILLIMETRES_PER_TICK_SQUARED);
    }
    else
    {
      m_speedMillimetresPerTick =
        std::min(MAX_SPEED_MILLIMETRES_PER_TICK, m_speedMillimetresPerTick + ACCELERATION_MILLIMETRES_PER_TICK_SQUARED);
    }

    // Arrival. If this tick's move would reach or pass the target, land exactly on it and stop.
    // Without this the ship oscillates around the target forever by a few millimetres a tick,
    // which at 20 Hz is a visible jitter rather than a rounding detail.
    if (facingTheTarget && distance <= m_speedMillimetresPerTick)
    {
      m_positionXMillimetres = m_targetXMillimetres;
      m_positionZMillimetres = m_targetZMillimetres;
      m_speedMillimetresPerTick = 0;
      m_hasOrder = false;
      return;
    }
  }

  // Then move. Integer division truncates towards zero, which is deterministic and is the only
  // property this needs; the millimetre it loses per tick at full speed is below the resolution
  // of a screen where one pixel is 125 mm.
  const Neuron::SineCosine direction = Neuron::SineCosineTurns16(m_headingTurns16);
  const std::int64_t speed = m_speedMillimetresPerTick;
  m_positionXMillimetres = Neuron::SaturatingAdd(m_positionXMillimetres, speed * direction.cosine / Neuron::TRIG_ONE);
  m_positionZMillimetres = Neuron::SaturatingAdd(m_positionZMillimetres, speed * direction.sine / Neuron::TRIG_ONE);
}

} // namespace Frontier
