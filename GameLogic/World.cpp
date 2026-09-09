// World.cpp -- the game's implementation of the simulation seam.

#include "pch.h"
#include "World.h"

namespace Frontier
{

void World::ApplyOrder(const Neuron::MoveToOrder& _order)
{
  m_ship.OrderMoveTo(_order.targetXMillimetres, _order.targetZMillimetres);
}

void World::Tick()
{
  m_ship.Tick();
  ++m_tick;
}

Neuron::ShipState World::Snapshot() const
{
  return Neuron::ShipState{
    .tick = m_tick,
    .positionXMillimetres = m_ship.PositionXMillimetres(),
    .positionZMillimetres = m_ship.PositionZMillimetres(),
    .speedMillimetresPerTick = m_ship.SpeedMillimetresPerTick(),
    .headingTurns16 = m_ship.HeadingTurns16(),
    .reserved = 0,
  };
}

} // namespace Frontier
