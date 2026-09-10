#pragma once

#include "Galaxy.h"
#include "MatchState.h"
#include "Rules.h"
#include "Simulation.h"

#include <cstdint>

namespace Frontier
{

/// The game's simulation: a generated galaxy, and the tick it is on.
///
/// It owns a `MatchState` and nothing else. Every rule that reads or writes one is a free function
/// over that value (ADR-004), so this class is a holder rather than a place where behavior
/// accumulates -- which is the property that makes the resolver testable without it.
///
/// It implements `Neuron::Simulation`, which is what lets `NeuronServer` own and tick it without
/// linking `GameLogic` (ADR-007).
///
/// TRANSITIONAL, AND SAY SO. `Simulation` still carries the MVP-01 interface -- `ApplyOrder` of a
/// `MoveToOrder`, and a `Snapshot` that returns a `ShipState`. There is no ship any more, so the
/// two order-and-replication methods below do the only honest thing available to them: nothing, and
/// a record carrying the tick. Slice 2 of `Design/Plans/MVP-02-TheLoop.md` narrows the seam to
/// `Snapshot(seat)` and deletes both records; slice 3 gives the orders somewhere to go. Until then
/// the executable still builds and still ticks a real match, and what it draws is a leftover.
class World final : public Neuron::Simulation
{
public:
  /// A seed that produces an accepted galaxy under `DEFAULT_RULES`, so a default-constructed World
  /// is a real match rather than an empty one. `NeuronServer` builds one of these through the
  /// composition root and cannot pass arguments (ADR-007).
  static constexpr std::uint64_t DEFAULT_SEED = 1;
  static constexpr SeatId DEFAULT_SEAT_COUNT = 8;

  World();
  World(std::uint64_t _seed, SeatId _seatCount, const Rules& _rules);

  void ApplyOrder(const Neuron::MoveToOrder& _order) override;
  void Tick() override;
  [[nodiscard]] Neuron::ShipState Snapshot() const override;

  [[nodiscard]] const MatchState& State() const noexcept
  {
    return m_state;
  }
  [[nodiscard]] const Rules& CurrentRules() const noexcept
  {
    return m_rules;
  }
  [[nodiscard]] std::uint64_t TickCount() const noexcept
  {
    return m_state.tick;
  }
  /// Whether the galaxy this World was constructed with was accepted. A rejected seed leaves the
  /// state as the generator left it, which is a thing a caller should be able to notice.
  [[nodiscard]] GenerationResult Generation() const noexcept
  {
    return m_generation;
  }

private:
  MatchState m_state;
  Rules m_rules = DEFAULT_RULES;
  GenerationResult m_generation = GenerationResult::Accepted;
};

} // namespace Frontier
