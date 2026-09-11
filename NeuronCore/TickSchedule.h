#pragma once

#include <cstdint>

namespace Neuron
{

/// A UTC instant, in whole seconds since the Unix epoch.
///
/// Seconds because the tick interval is hours and nothing here needs finer; integer because the
/// arithmetic below has to give the same answer everywhere; and a plain type rather than
/// `std::chrono` because this is the value that crosses into the schedule from an injected clock,
/// and one number is easier to fix in a test than a time point is.
using Instant = std::int64_t;

/// When the locks are, as arithmetic.
///
/// ADR-026. **It holds no clock and never reads one.** The caller passes the instant in, which is
/// what lets a test drive a three-week match in a loop and what keeps R16's no-wall-clock rule true
/// one layer further out than it strictly has to be: the simulation is forbidden a clock, and the
/// thing that decides *when* to call it does not want one either.
///
/// Locks are at `startedAt + n * intervalSeconds`. The phase of day falls out of `startedAt` rather
/// than being a separate field: a match started at 06:00 UTC on a six-hour tick locks at 06:00,
/// 12:00, 18:00 and 00:00, which is the one-pager's "four ticks a day at fixed UTC times".
class TickSchedule
{
public:
  constexpr TickSchedule(Instant _startedAt, std::uint32_t _intervalSeconds) noexcept
    : m_startedAt(_startedAt),
      m_intervalSeconds(_intervalSeconds == 0 ? 1 : _intervalSeconds)
  {
  }

  [[nodiscard]] constexpr Instant StartedAt() const noexcept
  {
    return m_startedAt;
  }

  [[nodiscard]] constexpr std::uint32_t IntervalSeconds() const noexcept
  {
    return m_intervalSeconds;
  }

  /// When tick `_tick` locks. Tick 0 locks one interval after the match started, because tick 0 is
  /// the state before anything was played and the first lock is what produces tick 1.
  [[nodiscard]] constexpr Instant LockOf(std::uint32_t _tick) const noexcept
  {
    return m_startedAt + static_cast<Instant>(_tick + 1) * m_intervalSeconds;
  }

  /// How many locks have passed at `_now`. Never negative: an instant before the match started is
  /// zero locks, not a lock owed from the past.
  [[nodiscard]] constexpr std::uint32_t LocksDueAt(Instant _now) const noexcept
  {
    if (_now < m_startedAt)
    {
      return 0;
    }
    return static_cast<std::uint32_t>((_now - m_startedAt) / m_intervalSeconds);
  }

  /// How many locks a simulation at `_resolvedTicks` still owes at `_now`.
  ///
  /// The answer is a COUNT and not a boolean, which is the whole reason this function exists. A
  /// server that slept through two locks owes two resolutions, not one: a match that skips a tick
  /// has a hole in its order list and stops replaying (ADR-024).
  [[nodiscard]] constexpr std::uint32_t LocksOwed(std::uint32_t _resolvedTicks, Instant _now) const noexcept
  {
    const std::uint32_t due = LocksDueAt(_now);
    return due > _resolvedTicks ? due - _resolvedTicks : 0;
  }

  /// Seconds until tick `_tick` locks, or zero if it already has. The client's countdown.
  [[nodiscard]] constexpr std::int64_t SecondsUntilLockOf(std::uint32_t _tick, Instant _now) const noexcept
  {
    const Instant at = LockOf(_tick);
    return at > _now ? at - _now : 0;
  }

private:
  Instant m_startedAt = 0;
  /// Never zero. A schedule with no interval would owe infinite locks at the first instant after it
  /// started, so the constructor floors it rather than letting a misconfiguration divide by zero.
  std::uint32_t m_intervalSeconds = 1;
};

} // namespace Neuron
