// Visibility.cpp -- the fog rule (ADR-017) and the per-seat snapshot (ADR-005).

#include "pch.h"
#include "Visibility.h"

#include <algorithm>

namespace Frontier
{

namespace
{

/// Total strength standing at a system, whoever owns it. This is what a scout learns by arriving:
/// how much force is there, not whose each part is.
[[nodiscard]] std::int32_t ResidentStrength(const MatchState& _state, SystemId _system) noexcept
{
  std::int32_t strength = 0;
  for (const Fleet& fleet : _state.fleets)
  {
    if (fleet.atSystem == _system)
    {
      strength += fleet.strength;
    }
  }
  return strength;
}

} // namespace

std::vector<bool> ObservedSystems(const MatchState& _state, const Rules& _rules, SeatId _seat)
{
  std::vector<bool> observed(_state.systems.size(), false);

  for (const Fleet& fleet : _state.fleets)
  {
    if (fleet.owner == _seat && fleet.atSystem != NO_SYSTEM && fleet.atSystem < observed.size())
    {
      observed[fleet.atSystem] = true;
    }
  }

  // Then expand outward one lane at a time. Breadth by rounds rather than a queue, so the result
  // depends on the graph and not on a traversal order (R16).
  const std::int32_t reach = std::max(0, _rules.scoutingRevealLanes);
  for (std::int32_t round = 0; round < reach; ++round)
  {
    const std::vector<bool> before = observed;
    for (const Lane& lane : _state.lanes)
    {
      if (before[lane.endA])
      {
        observed[lane.endB] = true;
      }
      if (before[lane.endB])
      {
        observed[lane.endA] = true;
      }
    }
  }

  return observed;
}

void ObserveAndRemember(MatchState& _state, const Rules& _rules)
{
  for (Seat& seat : _state.seats)
  {
    // A seat created before the last system was placed would have a short memory vector, and a
    // short one is a silent out-of-range read later. Growing it here rather than asserting is the
    // right call because new systems are a thing a later slice may add mid-match.
    seat.memory.resize(_state.systems.size());

    const std::vector<bool> observed = ObservedSystems(_state, _rules, seat.id);
    for (std::size_t index = 0; index < _state.systems.size(); ++index)
    {
      if (!observed[index])
      {
        continue;
      }

      const System& system = _state.systems[index];
      seat.memory[index] = SystemMemory{
        .observedTick = _state.tick,
        .yieldPerTick = system.yieldPerTick,
        .garrisonStrength = ResidentStrength(_state, system.id),
        .owner = system.owner,
        .known = true,
      };
    }
  }
}

Neuron::VisibleSnapshot VisibleSnapshotFor(const MatchState& _state, const Rules& _rules, SeatId _seat)
{
  Neuron::VisibleSnapshot snapshot;
  snapshot.tick = _state.tick;
  snapshot.endTick = _state.endTick;
  snapshot.sealedOpensTick = _state.sealedOpensTick;
  snapshot.seat = _seat;

  const std::vector<bool> observed = ObservedSystems(_state, _rules, _seat);
  const Seat* seat = nullptr;
  for (const Seat& candidate : _state.seats)
  {
    if (candidate.id == _seat)
    {
      seat = &candidate;
    }
  }

  // Every system, always: the topology is public, so a seat sees the whole graph at its authored
  // coordinates from tick one. What is graded below is only the contents.
  snapshot.systems.reserve(_state.systems.size());
  for (const System& system : _state.systems)
  {
    Neuron::SystemView view = {};
    view.systemId = system.id;
    view.xUnits = system.position.xUnits;
    view.yUnits = system.position.yUnits;
    view.kind = static_cast<std::uint8_t>(system.kind);
    view.owner = NO_SEAT;

    const bool seesNow = system.id < observed.size() && observed[system.id];
    const SystemMemory* remembered = nullptr;
    if (seat != nullptr && system.id < seat->memory.size() && seat->memory[system.id].known)
    {
      remembered = &seat->memory[system.id];
    }

    if (seesNow)
    {
      view.visibility = Neuron::Visibility::Observed;
      view.owner = system.owner;
      view.yieldPerTick = system.yieldPerTick;
      view.garrisonStrength = ResidentStrength(_state, system.id);
      view.observedTick = _state.tick;
    }
    else if (remembered != nullptr)
    {
      view.visibility = Neuron::Visibility::Remembered;
      view.owner = remembered->owner;
      view.yieldPerTick = remembered->yieldPerTick;
      view.garrisonStrength = remembered->garrisonStrength;
      view.observedTick = remembered->observedTick;
    }
    else
    {
      // Unknown, and every content field stays zero. Not a guess, not a default that could be
      // mistaken for a reading: a seat is shown what it saw, or nothing.
      view.visibility = Neuron::Visibility::Unknown;
    }

    snapshot.systems.push_back(view);
  }

  snapshot.lanes.reserve(_state.lanes.size());
  for (const Lane& lane : _state.lanes)
  {
    snapshot.lanes.push_back(Neuron::LaneView{
      .laneId = lane.id,
      .endA = lane.endA,
      .endB = lane.endB,
      .costTicks = lane.costTicks,
    });
  }

  // Fleets in transit, all of them, whoever owns them. This is the one-pager's rule and not an
  // oversight: once a fleet departs it is visible on its lane with its tick-ETA, which is what
  // makes a commitment blind when it is made and public once it is.
  for (const Fleet& fleet : _state.fleets)
  {
    if (fleet.onLane == NO_LANE)
    {
      continue;
    }
    snapshot.transits.push_back(Neuron::TransitView{
      .fleetId = fleet.id,
      .laneId = fleet.onLane,
      .towardSystemId = fleet.towardSystem,
      .owner = fleet.owner,
      .reserved = 0,
      .strength = fleet.strength,
      .departedTick = fleet.departedTick,
      .arrivesTick = fleet.arrivesTick,
    });
  }

  snapshot.seats.reserve(_state.seats.size());
  for (const Seat& other : _state.seats)
  {
    snapshot.seats.push_back(Neuron::SeatView{
      .seatId = other.id,
      .reserved0 = 0,
      .capitalSystemId = other.capital,
      // Scoring is slice 7's. Zero here is a placeholder and is visibly one; inventing a formula
      // now would put a number on screen that no rule produced.
      .score = 0,
      .capitalGuardEndsTick = _rules.capitalGuardTicks,
    });
  }

  return snapshot;
}

} // namespace Frontier
