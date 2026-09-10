// World.cpp -- the game's implementation of the simulation seam (ADR-007).

#include "pch.h"
#include "World.h"

namespace Frontier
{

World::World()
  : World(DEFAULT_SEED, DEFAULT_SEAT_COUNT, DEFAULT_RULES)
{
}

World::World(std::uint64_t _seed, SeatId _seatCount, const Rules& _rules)
  : m_rules(_rules)
{
  m_generation = GenerateGalaxy(_seed, _seatCount, m_rules, m_state);
}

void World::ApplyOrder(const Neuron::MoveToOrder& _order)
{
  // Nothing to apply. The 4X's orders are a fleet and a system, not a point (ADR-015), and they do
  // not exist until slice 3 of the plan. Discarding a MoveToOrder is the honest response to a
  // record this game no longer has a meaning for; the alternative -- inventing one -- is how a
  // transitional stub becomes a permanent feature.
  static_cast<void>(_order);
}

void World::Tick()
{
  // Advance the clock, and nothing else. `Resolve` and its six phases are slice 2's and slice 3's
  // work (ADR-004); until then a tick is the passage of time and no rule reads it.
  ++m_state.tick;
}

Neuron::ShipState World::Snapshot() const
{
  // The tick, and zeros. Everything else in this record describes a ship, and there is no ship.
  // Slice 2 replaces it with the per-seat VisibleSnapshot of ADR-005.
  return Neuron::ShipState{
    .tick = m_state.tick,
    .positionXMillimetres = 0,
    .positionZMillimetres = 0,
    .speedMillimetresPerTick = 0,
    .headingTurns16 = 0,
    .reserved = 0,
  };
}

} // namespace Frontier
