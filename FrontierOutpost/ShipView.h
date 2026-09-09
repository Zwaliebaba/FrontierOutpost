#pragma once

#include "Protocol.h"

#include <cstdint>

namespace Frontier
{

/// The client's picture of the ship: the last two states the server sent, and where between them
/// to draw it (ADR-005).
///
/// This is the whole of the client's opinion about where the ship is, and it holds no rules. It
/// cannot advance the ship, it cannot predict, and it cannot move it because a key was pressed:
/// the only thing that changes what it says is a state arriving from the server. That is the
/// property MVP-01 section 2 is protecting, and keeping the arithmetic here rather than in the
/// renderer is what makes it checkable.
///
/// It INTERPOLATES and never extrapolates. At 20 Hz that costs one tick -- 50 ms -- of latency,
/// which is the price of never showing a position the server did not actually compute. An
/// extrapolating client is smoother right up to the moment the ship stops, and then it overshoots
/// and snaps back, which is worse than being 50 ms late.
class ShipView
{
public:
  /// Real seconds in one server tick. The client has to know the server's tick rate to know how
  /// long to spread an interpolation over; this is the one number that crosses.
  static constexpr float TICK_SECONDS = 1.0F / 20.0F;

  /// A state that arrived from the transport. States arrive in order over a loopback, so this
  /// keeps whichever is newest and does not reorder.
  void Accept(const Neuron::ShipState& _state) noexcept;

  /// Real time since the last frame. Interpolation runs on the client's clock rather than the
  /// server's, because the client draws whenever the display lets it.
  void Advance(float _elapsedSeconds) noexcept;

  /// False until the first state has arrived. The client draws no ship at all until then rather
  /// than drawing one at the origin, which would be a position nobody asserted (ADR-005).
  [[nodiscard]] bool HasState() const noexcept
  {
    return m_stateCount > 0;
  }

  /// Metres, for the renderer. The simulation works in millimetres and the screen in pixels; this
  /// is the one place the two meet, and it is a division by a thousand rather than a scale factor
  /// somebody has to remember.
  [[nodiscard]] float PositionXMetres() const noexcept;
  [[nodiscard]] float PositionZMetres() const noexcept;
  [[nodiscard]] float HeadingRadians() const noexcept;

  /// The newest tick the server has reported, for the status line.
  [[nodiscard]] std::uint64_t Tick() const noexcept
  {
    return m_latest.tick;
  }

  [[nodiscard]] std::int32_t SpeedMillimetresPerTick() const noexcept
  {
    return m_latest.speedMillimetresPerTick;
  }

private:
  /// 0 at the previous state, 1 at the latest. Clamped at 1, so a client that stops hearing from
  /// the server freezes on the last position it was told about instead of sliding past it.
  [[nodiscard]] float Fraction() const noexcept;

  Neuron::ShipState m_previous = {};
  Neuron::ShipState m_latest = {};
  std::uint32_t m_stateCount = 0;
  float m_secondsSinceLatest = 0.0F;
};

} // namespace Frontier
