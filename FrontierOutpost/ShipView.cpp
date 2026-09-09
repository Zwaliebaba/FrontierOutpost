// ShipView.cpp -- interpolation between the last two replicated states (ADR-005).

#include "pch.h"
#include "ShipView.h"

#include "Trigonometry.h"

#include <numbers>

namespace Frontier
{

namespace
{

constexpr float MILLIMETRES_PER_METRE = 1000.0F;

[[nodiscard]] float Lerp(std::int64_t _from, std::int64_t _to, float _fraction) noexcept
{
  const auto from = static_cast<float>(_from);
  return from + (static_cast<float>(_to) - from) * _fraction;
}

} // namespace

void ShipView::Accept(const Neuron::ShipState& _state) noexcept
{
  // The very first state has nothing to interpolate from, so it becomes both ends and the ship is
  // drawn exactly there. Everything after it shifts the window along by one.
  m_previous = m_stateCount == 0 ? _state : m_latest;
  m_latest = _state;
  m_secondsSinceLatest = 0.0F;

  if (m_stateCount < 2)
  {
    ++m_stateCount;
  }
}

void ShipView::Advance(float _elapsedSeconds) noexcept
{
  m_secondsSinceLatest += _elapsedSeconds;
}

float ShipView::Fraction() const noexcept
{
  return std::min(1.0F, m_secondsSinceLatest / TICK_SECONDS);
}

float ShipView::PositionXMetres() const noexcept
{
  return Lerp(m_previous.positionXMillimetres, m_latest.positionXMillimetres, Fraction()) / MILLIMETRES_PER_METRE;
}

float ShipView::PositionZMetres() const noexcept
{
  return Lerp(m_previous.positionZMillimetres, m_latest.positionZMillimetres, Fraction()) / MILLIMETRES_PER_METRE;
}

float ShipView::HeadingRadians() const noexcept
{
  // Interpolating a heading is not interpolating a number: 65000 to 500 is a turn of 1000 units
  // forwards, not 64500 backwards. ShortestTurnTurns16 is the difference that knows that, and
  // adding a fraction of it to the start is the rotation the server actually performed.
  const std::int32_t difference = Neuron::ShortestTurnTurns16(m_previous.headingTurns16, m_latest.headingTurns16);
  const float turns16 = static_cast<float>(m_previous.headingTurns16) + static_cast<float>(difference) * Fraction();

  constexpr float RADIANS_PER_TURNS16 = 2.0F * std::numbers::pi_v<float> / static_cast<float>(Neuron::TURNS16_PER_TURN);
  return turns16 * RADIANS_PER_TURNS16;
}

} // namespace Frontier
