#pragma once

// CountingSimulation.h -- the fake every server test drives.
//
// ADR-007's argument, carried forward by ADR-025: `NeuronServer` references `NeuronCore` and
// nothing else, so the only simulation its tests can be written against is one written here. The
// payoff is that these tests fail for server reasons -- a lock missed, a store truncated, a reload
// that did not reproduce -- and never because somebody changed a combat rule.
//
// It is a header because two test files need it, and a fake with two users is a fake that has
// earned one.

#include "Simulation.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace NeuronServerTests
{

/// How many players the fake has. Four, because the tests are about routing rather than about a
/// galaxy, and four is enough to tell player one from player three.
inline constexpr std::int32_t PLAYERS = 4;

/// A simulation that counts what was done to it and has no rules at all.
///
/// Its "state" is the sequence of turns it was given, hashed. That is enough to make a reload test
/// meaningful -- replaying the same turns in the same order must produce the same hash, and
/// replaying them in a different order must not -- without this file knowing what a fleet is.
class CountingSimulation final : public Neuron::Simulation
{
public:
  explicit CountingSimulation(std::uint64_t _seed = 7)
    : m_seed(_seed)
  {
    m_pending.assign(PLAYERS, Neuron::PlayerTurn{});
  }

  [[nodiscard]] std::int32_t PlayerCount() const override
  {
    return PLAYERS;
  }
  [[nodiscard]] std::uint32_t Tick() const override
  {
    return m_tick;
  }
  [[nodiscard]] bool IsFinished() const override
  {
    return m_tick >= m_length;
  }
  [[nodiscard]] std::uint64_t Hash() const override
  {
    return m_hash;
  }

  [[nodiscard]] std::vector<std::uint8_t> Configuration() const override
  {
    return {static_cast<std::uint8_t>(m_seed & 0xFFU), static_cast<std::uint8_t>(m_length & 0xFFU)};
  }

  void Submit(std::int32_t _player, std::span<const std::uint8_t> _orders) override
  {
    if (_player < 0 || _player >= PLAYERS)
    {
      ++submissionsRefused;
      return;
    }
    m_pending[static_cast<std::size_t>(_player)].orders.assign(_orders.begin(), _orders.end());
    ++submissions;
  }

  void MarkPresent(std::int32_t _player) override
  {
    if (_player >= 0 && _player < PLAYERS)
    {
      m_pending[static_cast<std::size_t>(_player)].present = true;
    }
  }

  void Resolve() override
  {
    ++resolves;
    ++m_tick;

    // The hash folds in the turns IN ORDER, so a reload that replayed them out of order, dropped
    // one, or lost a presence mark produces a different number -- which is exactly what the
    // session's reload check is supposed to catch.
    for (const Neuron::PlayerTurn& turn : m_pending)
    {
      m_hash = m_hash * 1099511628211ULL + (turn.present ? 1U : 2U);
      for (const std::uint8_t byte : turn.orders)
      {
        m_hash = m_hash * 1099511628211ULL + byte;
      }
      m_hash = m_hash * 1099511628211ULL + 0xFFU;
    }

    m_locked = std::move(m_pending);
    m_pending.assign(PLAYERS, Neuron::PlayerTurn{});
  }

  [[nodiscard]] std::vector<Neuron::PlayerTurn> LockedTurn() const override
  {
    return m_locked;
  }

  [[nodiscard]] std::vector<std::uint8_t> SnapshotFor(std::int32_t _player) const override
  {
    return {static_cast<std::uint8_t>(_player), static_cast<std::uint8_t>(m_tick & 0xFFU)};
  }

  [[nodiscard]] std::vector<std::uint8_t> DigestFor(std::int32_t _player) const override
  {
    return {static_cast<std::uint8_t>(0xD0), static_cast<std::uint8_t>(_player)};
  }

  void SetLength(std::uint32_t _ticks) noexcept
  {
    m_length = _ticks;
  }

  std::uint32_t resolves = 0;
  std::uint32_t submissions = 0;
  std::uint32_t submissionsRefused = 0;

private:
  std::uint64_t m_seed = 0;
  std::uint64_t m_hash = 0xCBF29CE484222325ULL;
  std::uint32_t m_tick = 0;
  std::uint32_t m_length = 1000;

  std::vector<Neuron::PlayerTurn> m_pending;
  std::vector<Neuron::PlayerTurn> m_locked;
};

} // namespace NeuronServerTests
