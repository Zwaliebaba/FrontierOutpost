#pragma once

#include "Protocol.h"

namespace Neuron
{

/// What a Session ticks. The seam between the engine's server and the game's rules (ADR-007).
///
/// This exists because of a collision worth writing down. MVP-01 step 5 says "NeuronServer: a
/// Session that owns a World", and AGENTS.md 2 says GameLogic is referenced by the executable and
/// by nothing else -- NeuronServer references NeuronCore and nothing more. Both cannot be true as
/// written: a Session that owns a Frontier::World is a NeuronServer that links GameLogic.
///
/// The seam resolves it without giving up either. Session owns a Simulation, which is declared
/// here in NeuronCore -- the one library both halves already share. Frontier::World implements it.
/// FrontierOutpost.exe, which is allowed to know about both, is what puts them together. The
/// server still owns the simulation and still ticks it; it just does not know what it is.
///
/// R2 warns that a base class for one derived class is ceremony. This one is not: the abstraction
/// is not there to anticipate a second implementation, it is there because the alternative is an
/// edge in the dependency graph that AGENTS.md forbids. The test suites are the second
/// implementation anyway, the day one of them wants a simulation that does nothing.
class Simulation
{
public:
  virtual ~Simulation() = default;

  Simulation(const Simulation&) = delete;
  Simulation& operator=(const Simulation&) = delete;
  Simulation(Simulation&&) = delete;
  Simulation& operator=(Simulation&&) = delete;

  /// An order that arrived from a client. Applied before the tick it arrived for, so an order and
  /// the tick it takes effect on are never ambiguous.
  virtual void ApplyOrder(const MoveToOrder& _order) = 0;

  /// One fixed step. The tick is the clock (R16): nothing in here may read a wall clock, and the
  /// step size is not a parameter because a simulation whose step varies is one that gives a
  /// different answer on a slower machine.
  virtual void Tick() = 0;

  /// What to replicate after the tick just run.
  [[nodiscard]] virtual ShipState Snapshot() const = 0;

protected:
  Simulation() = default;
};

} // namespace Neuron
