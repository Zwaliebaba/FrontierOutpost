#pragma once

#include "Ship.h"
#include "Simulation.h"

#include <cstdint>

namespace Frontier
{

/// The game's simulation: one ship, for now.
///
/// It implements Neuron::Simulation, which is what lets NeuronServer own and tick it without
/// linking GameLogic (ADR-007). The engine sees Tick, ApplyOrder and Snapshot; what a ship is
/// stays on this side of the seam.
class World final : public Neuron::Simulation
{
public:
  void ApplyOrder(const Neuron::MoveToOrder& _order) override;
  void Tick() override;
  [[nodiscard]] Neuron::ShipState Snapshot() const override;

  [[nodiscard]] const Ship& PlayerShip() const noexcept
  {
    return m_ship;
  }
  [[nodiscard]] std::uint64_t TickCount() const noexcept
  {
    return m_tick;
  }

private:
  Ship m_ship;
  /// Ticks since the world was created. The clock (R16): nothing here reads a wall clock, and
  /// this is the only thing that advances.
  std::uint64_t m_tick = 0;
};

} // namespace Frontier
