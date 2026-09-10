// Galaxy.cpp -- ADR-014's generator: concentric, planar by construction, and lane costs read off
// the drawn lengths so the map cannot lie about travel time.
//
// The order of business is: place every system, join them with lanes whose cost comes from their
// length, then check the whole thing against every guarantee the one-pager and ADR-014 state. The
// checks are a safety net and not a search -- the placement is built to satisfy them, and the
// measured rejection rate says how well (GameLogicTests reports it).

#include "pch.h"
#include "Galaxy.h"

#include "Random.h"
#include "Trigonometry.h"
#include "Visibility.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace Frontier
{

namespace
{

/// Which side of the line a->b the point c falls on: 1, -1, or 0 for collinear.
[[nodiscard]] std::int32_t Orientation(const MapPoint& _a, const MapPoint& _b, const MapPoint& _c) noexcept
{
  const std::int64_t abX = static_cast<std::int64_t>(_b.xUnits) - _a.xUnits;
  const std::int64_t abY = static_cast<std::int64_t>(_b.yUnits) - _a.yUnits;
  const std::int64_t acX = static_cast<std::int64_t>(_c.xUnits) - _a.xUnits;
  const std::int64_t acY = static_cast<std::int64_t>(_c.yUnits) - _a.yUnits;
  const std::int64_t cross = abX * acY - abY * acX;

  return cross > 0 ? 1 : (cross < 0 ? -1 : 0);
}

[[nodiscard]] bool SamePoint(const MapPoint& _a, const MapPoint& _b) noexcept
{
  return _a.xUnits == _b.xUnits && _a.yUnits == _b.yUnits;
}

/// True when two lane segments meet anywhere other than at a shared endpoint.
///
/// The collinear case is the one that matters here and the one that was wrong first. Two radial
/// spokes half a turn apart lie on the same line -- capital 0's spoke runs inward along +x, capital
/// 3's inward along -x -- and every orientation test returns zero for all four points. Reporting
/// that as a crossing rejected a fifth of all seeds for a picture that is perfectly fine. Collinear
/// segments conflict only when they overlap over a positive length.
[[nodiscard]] bool ProperlyCross(const MapPoint& _p1, const MapPoint& _p2, const MapPoint& _p3, const MapPoint& _p4) noexcept
{
  if (SamePoint(_p1, _p3) || SamePoint(_p1, _p4) || SamePoint(_p2, _p3) || SamePoint(_p2, _p4))
  {
    return false;
  }

  const std::int32_t d1 = Orientation(_p3, _p4, _p1);
  const std::int32_t d2 = Orientation(_p3, _p4, _p2);
  const std::int32_t d3 = Orientation(_p1, _p2, _p3);
  const std::int32_t d4 = Orientation(_p1, _p2, _p4);

  if (d1 * d2 < 0 && d3 * d4 < 0)
  {
    return true;
  }

  if (d1 == 0 && d2 == 0 && d3 == 0 && d4 == 0)
  {
    const std::int64_t axisX = static_cast<std::int64_t>(_p2.xUnits) - _p1.xUnits;
    const std::int64_t axisY = static_cast<std::int64_t>(_p2.yUnits) - _p1.yUnits;
    const auto project = [&](const MapPoint& _q) noexcept
    { return (static_cast<std::int64_t>(_q.xUnits) - _p1.xUnits) * axisX + (static_cast<std::int64_t>(_q.yUnits) - _p1.yUnits) * axisY; };

    const std::int64_t firstHigh = project(_p2);
    std::int64_t secondLow = project(_p3);
    std::int64_t secondHigh = project(_p4);
    if (secondLow > secondHigh)
    {
      std::swap(secondLow, secondHigh);
    }

    return std::min(firstHigh, secondHigh) - std::max<std::int64_t>(0, secondLow) > 0;
  }

  return false;
}

/// The cost band a drawn length falls in, or 0 for a length past the last band.
///
/// This function is ADR-014's monotonicity guarantee, and it is the whole of it: a lane's cost is
/// read off its length, so a longer lane can never come out cheaper. Nothing else in the generator
/// assigns a cost.
[[nodiscard]] std::int32_t CostForLengthSquared(std::int64_t _lengthSquaredUnits, const Rules& _rules) noexcept
{
  for (std::size_t band = 0; band < _rules.laneCostBandUpperUnits.size(); ++band)
  {
    const std::int64_t upper = _rules.laneCostBandUpperUnits[band];
    if (_lengthSquaredUnits <= upper * upper)
    {
      return static_cast<std::int32_t>(band) + 1;
    }
  }

  return 0;
}

/// The two systems are in the same starting cluster, so their lane must be one tick.
[[nodiscard]] bool SameCluster(const System& _a, const System& _b) noexcept
{
  return _a.homeSeat != NO_SEAT && _a.homeSeat == _b.homeSeat;
}

/// Every check ADR-014 and the one-pager ask of a placed galaxy, in the order that reports the most
/// specific cause.
[[nodiscard]] GenerationResult Validate(const MatchState& _state, const Rules& _rules)
{
  const std::int64_t minSeparationSquared = static_cast<std::int64_t>(_rules.minSeparationUnits) * _rules.minSeparationUnits;
  for (std::size_t a = 0; a < _state.systems.size(); ++a)
  {
    for (std::size_t b = a + 1; b < _state.systems.size(); ++b)
    {
      if (DistanceSquaredUnits(_state.systems[a].position, _state.systems[b].position) < minSeparationSquared)
      {
        return GenerationResult::SeparationTooSmall;
      }
    }
  }

  for (const Lane& lane : _state.lanes)
  {
    if (lane.costTicks == 0)
    {
      return GenerationResult::LaneTooLong;
    }

    const bool intra = SameCluster(_state.systems[lane.endA], _state.systems[lane.endB]);
    if (intra && lane.costTicks != 1)
    {
      return GenerationResult::ClusterLaneNotOneTick;
    }
    if (!intra && lane.costTicks < 2)
    {
      return GenerationResult::FrontierLaneUnderTwoTicks;
    }
  }

  for (const Lane& first : _state.lanes)
  {
    for (const Lane& second : _state.lanes)
    {
      if (first.costTicks < second.costTicks && LaneLengthSquaredUnits(_state, first) > LaneLengthSquaredUnits(_state, second))
      {
        return GenerationResult::LengthNotMonotoneInCost;
      }
    }
  }

  for (std::size_t i = 0; i < _state.lanes.size(); ++i)
  {
    for (std::size_t j = i + 1; j < _state.lanes.size(); ++j)
    {
      const Lane& first = _state.lanes[i];
      const Lane& second = _state.lanes[j];
      if (ProperlyCross(_state.systems[first.endA].position, _state.systems[first.endB].position, _state.systems[second.endA].position,
                        _state.systems[second.endB].position))
      {
        return GenerationResult::LanesCross;
      }
    }
  }

  if (_state.seats.empty())
  {
    return GenerationResult::Disconnected;
  }

  const std::vector<std::int32_t> fromFirst = ShortestPathTicksFrom(_state, _state.seats[0].capital);
  if (std::ranges::find(fromFirst, -1) != fromFirst.end())
  {
    return GenerationResult::Disconnected;
  }

  for (const Seat& seat : _state.seats)
  {
    const std::vector<std::int32_t> reach = ShortestPathTicksFrom(_state, seat.capital);
    std::int32_t nearestRival = -1;
    for (const Seat& other : _state.seats)
    {
      if (other.id == seat.id)
      {
        continue;
      }
      const std::int32_t cost = reach[other.capital];
      if (cost >= 0 && (nearestRival < 0 || cost < nearestRival))
      {
        nearestRival = cost;
      }
    }

    if (nearestRival < 0 || nearestRival > _rules.capitalRivalMaxTicks)
    {
      return GenerationResult::NoRivalCapitalWithinReach;
    }
  }

  return GenerationResult::Accepted;
}

} // namespace

const char* Describe(GenerationResult _result) noexcept
{
  switch (_result)
  {
  case GenerationResult::Accepted:
    return "accepted";
  case GenerationResult::SeparationTooSmall:
    return "two systems closer than the minimum separation";
  case GenerationResult::LaneTooLong:
    return "a lane longer than the last cost band";
  case GenerationResult::ClusterLaneNotOneTick:
    return "a lane inside a starting cluster is not one tick";
  case GenerationResult::FrontierLaneUnderTwoTicks:
    return "a lane outside a starting cluster is under two ticks";
  case GenerationResult::LengthNotMonotoneInCost:
    return "a longer lane costs less than a shorter one";
  case GenerationResult::LanesCross:
    return "two lanes cross";
  case GenerationResult::Disconnected:
    return "a system cannot be reached from a capital";
  case GenerationResult::NoRivalCapitalWithinReach:
    return "a capital has no rival capital within reach";
  }

  return "unknown";
}

GenerationResult GenerateGalaxy(std::uint64_t _seed, SeatId _seatCount, const Rules& _rules, MatchState& _outState)
{
  _outState = MatchState{};
  _outState.seed = _seed;
  _outState.endTick = _rules.matchLengthTicks;
  _outState.sealedOpensTick = _rules.sealedOpensTick;

  const auto seats = static_cast<std::int32_t>(_seatCount);
  if (seats < 2 || _rules.sealedSystemCount < 1)
  {
    return GenerationResult::Disconnected;
  }

  Random random{_seed};

  // Radii. The capital chord is the number that matters: it is sampled inside cost band 3, and a
  // direct lane between adjacent capitals is therefore what delivers the one-pager's guarantee of a
  // rival within three ticks. Everything else is measured inward from it, so the galaxy grows with
  // the seat count -- "one bounded galaxy sized to the number of humans" -- while the lane costs
  // between neighbors stay put.
  const auto halfSector = static_cast<Neuron::Turns16>(32768 / seats);
  const Neuron::SineCosine halfAngle = Neuron::SineCosineTurns16(halfSector);
  const std::int32_t chordUnits = random.Between(_rules.capitalChordMinUnits, _rules.capitalChordMaxUnits);
  // Both halves of the division are widened before the arithmetic rather than after it: the
  // divisor cannot overflow an int at these magnitudes, but a multiplication performed in int
  // and then widened is the shape of the defect bugprone-implicit-widening-of-multiplication-result
  // exists to catch, and the check is fatal here.
  const std::int64_t chordScaled = static_cast<std::int64_t>(chordUnits) * Neuron::TRIG_ONE;
  const auto outerRadiusUnits = static_cast<std::int32_t>(chordScaled / (2LL * halfAngle.sine));
  const std::int32_t frontierRadiusUnits = outerRadiusUnits - _rules.frontierRadialGapUnits;
  const std::int32_t innerGapUnits = random.Between(_rules.sealedInnerGapMinUnits, _rules.sealedInnerGapMaxUnits);
  const std::int32_t sealedRadiusUnits =
    std::max(_rules.sealedRadiusMinUnits, std::min(_rules.sealedRadiusMaxUnits, frontierRadiusUnits - innerGapUnits));

  const std::int32_t sectorTurns16 = 65536 / seats;

  const auto scale = [](std::int32_t _radiusUnits, std::int32_t _trig) noexcept
  { return static_cast<std::int32_t>(static_cast<std::int64_t>(_radiusUnits) * _trig / Neuron::TRIG_ONE); };

  const auto addSystem = [&](std::int32_t _xUnits, std::int32_t _yUnits, SystemKind _kind, SeatId _homeSeat,
                             std::int32_t _yieldPerTick) -> SystemId
  {
    // The two jitters are drawn into locals, in this order, so the sequence the generator pulls out
    // of the stream is a property of this function rather than of an argument evaluation order the
    // standard leaves unspecified.
    const std::int32_t jitterX = random.Between(-_rules.positionJitterUnits, _rules.positionJitterUnits);
    const std::int32_t jitterY = random.Between(-_rules.positionJitterUnits, _rules.positionJitterUnits);

    const auto id = static_cast<SystemId>(_outState.systems.size());
    _outState.systems.push_back(System{
      .id = id,
      .position = MapPoint{_xUnits + jitterX, _yUnits + jitterY},
      .kind = _kind,
      .homeSeat = _homeSeat,
      .owner = NO_SEAT,
      .yieldPerTick = _yieldPerTick,
    });
    return id;
  };

  const auto addLane = [&](SystemId _endA, SystemId _endB)
  {
    const auto id = static_cast<LaneId>(_outState.lanes.size());
    const std::int64_t lengthSquared = DistanceSquaredUnits(_outState.systems[_endA].position, _outState.systems[_endB].position);
    _outState.lanes.push_back(Lane{
      .id = id,
      .endA = _endA,
      .endB = _endB,
      .costTicks = CostForLengthSquared(lengthSquared, _rules),
    });
  };

  // The outer ring: one capital per seat, each with an outward arc of cluster systems. Outward is
  // deliberate -- the capital-to-capital chords run inside the ring, so a cluster placed outside it
  // cannot have a lane that crosses one.
  std::vector<SystemId> capitals;
  capitals.reserve(static_cast<std::size_t>(seats));
  for (std::int32_t seat = 0; seat < seats; ++seat)
  {
    const auto seatId = static_cast<SeatId>(seat);
    const auto ringAngle = static_cast<Neuron::Turns16>(sectorTurns16 * seat);
    const Neuron::SineCosine ring = Neuron::SineCosineTurns16(ringAngle);

    const SystemId capital = addSystem(scale(outerRadiusUnits, ring.cosine), scale(outerRadiusUnits, ring.sine), SystemKind::Capital,
                                       seatId, _rules.capitalYieldPerTick);
    capitals.push_back(capital);

    const std::int32_t satelliteCount = random.Between(_rules.satelliteMinCount, _rules.satelliteMaxCount);
    std::vector<SystemId> satellites;
    satellites.reserve(static_cast<std::size_t>(satelliteCount));
    for (std::int32_t index = 0; index < satelliteCount; ++index)
    {
      // Whole half-steps either side of the outward radial: -(n-1), -(n-3) ... +(n-1). No float
      // enters the placement, which is what R16 is about even here where it would be harmless.
      const std::int32_t halfSteps = 2 * index - (satelliteCount - 1);
      const std::int32_t offsetTurns16 = halfSteps * _rules.satelliteSpreadTurns16 / 2;
      const auto spokeAngle = static_cast<Neuron::Turns16>(static_cast<std::int32_t>(ringAngle) + offsetTurns16);
      const Neuron::SineCosine spoke = Neuron::SineCosineTurns16(spokeAngle);

      const MapPoint base = _outState.systems[capital].position;
      const std::int32_t yieldPerTick = random.Between(_rules.clusterYieldMinPerTick, _rules.clusterYieldMaxPerTick);
      satellites.push_back(addSystem(base.xUnits + scale(_rules.satelliteRadiusUnits, spoke.cosine),
                                     base.yUnits + scale(_rules.satelliteRadiusUnits, spoke.sine), SystemKind::Cluster, seatId,
                                     yieldPerTick));
    }

    for (const SystemId satellite : satellites)
    {
      addLane(capital, satellite);
    }
    for (std::size_t index = 1; index < satellites.size(); ++index)
    {
      addLane(satellites[index - 1], satellites[index]);
    }
  }

  // The lane that makes first contact happen on day one, and the reason a rival capital is three
  // ticks away rather than four: capitals are joined directly.
  for (std::int32_t seat = 0; seat < seats; ++seat)
  {
    addLane(capitals[static_cast<std::size_t>(seat)], capitals[static_cast<std::size_t>((seat + 1) % seats)]);
  }

  // The frontier ring: one node per sector, a radial spoke inward from its capital, and a ring
  // cycle. Only radial spokes and only same-sector ones, because a diagonal to a neighbor's sector
  // is the one lane that would cross another.
  std::vector<SystemId> frontier;
  frontier.reserve(static_cast<std::size_t>(seats));
  for (std::int32_t seat = 0; seat < seats; ++seat)
  {
    const auto ringAngle = static_cast<Neuron::Turns16>(sectorTurns16 * seat);
    const Neuron::SineCosine ring = Neuron::SineCosineTurns16(ringAngle);
    const std::int32_t yieldPerTick = random.Between(_rules.frontierYieldMinPerTick, _rules.frontierYieldMaxPerTick);
    frontier.push_back(addSystem(scale(frontierRadiusUnits, ring.cosine), scale(frontierRadiusUnits, ring.sine), SystemKind::Frontier,
                                 NO_SEAT, yieldPerTick));
  }
  for (std::int32_t seat = 0; seat < seats; ++seat)
  {
    addLane(capitals[static_cast<std::size_t>(seat)], frontier[static_cast<std::size_t>(seat)]);
  }
  for (std::int32_t seat = 0; seat < seats; ++seat)
  {
    addLane(frontier[static_cast<std::size_t>(seat)], frontier[static_cast<std::size_t>((seat + 1) % seats)]);
  }

  // The sealed region: a small ring at the center, visible from tick one and enterable by nobody in
  // MVP-02. Each site is spoked out to the frontier node nearest it in angle, which keeps the
  // segment near-radial and so clear of the frontier ring's own edges.
  const std::int32_t sealedCount = _rules.sealedSystemCount;
  const std::int32_t sealedSectorTurns16 = 65536 / sealedCount;
  std::vector<SystemId> sealed;
  sealed.reserve(static_cast<std::size_t>(sealedCount));
  for (std::int32_t site = 0; site < sealedCount; ++site)
  {
    const auto siteAngle = static_cast<Neuron::Turns16>(sealedSectorTurns16 * site);
    const Neuron::SineCosine ring = Neuron::SineCosineTurns16(siteAngle);
    const std::int32_t yieldPerTick = random.Between(_rules.sealedYieldMinPerTick, _rules.sealedYieldMaxPerTick);
    sealed.push_back(
      addSystem(scale(sealedRadiusUnits, ring.cosine), scale(sealedRadiusUnits, ring.sine), SystemKind::Sealed, NO_SEAT, yieldPerTick));
  }
  for (std::size_t site = 0; site + 1 < sealed.size(); ++site)
  {
    addLane(sealed[site], sealed[site + 1]);
  }
  if (sealed.size() > 2)
  {
    addLane(sealed.back(), sealed.front());
  }

  std::vector<bool> frontierTaken(static_cast<std::size_t>(seats), false);
  for (std::int32_t site = 0; site < sealedCount; ++site)
  {
    const std::int32_t wantTurns16 = sealedSectorTurns16 * site;
    std::size_t best = frontier.size();
    std::int32_t bestSeparation = 65536;
    for (std::int32_t seat = 0; seat < seats; ++seat)
    {
      if (frontierTaken[static_cast<std::size_t>(seat)])
      {
        continue;
      }
      const std::int32_t delta = (sectorTurns16 * seat - wantTurns16) & 0xFFFF;
      const std::int32_t separation = std::min(delta, 65536 - delta);
      if (separation < bestSeparation)
      {
        best = static_cast<std::size_t>(seat);
        bestSeparation = separation;
      }
    }

    if (best == frontier.size())
    {
      return GenerationResult::Disconnected;
    }
    frontierTaken[best] = true;
    addLane(sealed[static_cast<std::size_t>(site)], frontier[best]);
  }

  // Seats, and the pinned fleet each capital starts with (ADR-018).
  for (std::int32_t seat = 0; seat < seats; ++seat)
  {
    const auto seatId = static_cast<SeatId>(seat);
    const SystemId capital = capitals[static_cast<std::size_t>(seat)];
    _outState.systems[capital].owner = seatId;
    _outState.seats.push_back(Seat{
      .id = seatId, .capital = capital, .income = _rules.startingIncome, .memory = std::vector<SystemMemory>(_outState.systems.size())});

    _outState.fleets.push_back(Fleet{
      .id = static_cast<FleetId>(_outState.fleets.size()),
      .owner = seatId,
      .atSystem = capital,
      .onLane = NO_LANE,
      .towardSystem = NO_SYSTEM,
      .departedTick = 0,
      .arrivesTick = 0,
      .strength = _rules.startingGarrisonStrength,
      .pinned = true,
    });
  }

  // A seat starts knowing its own cluster, because its garrison is standing in it. Without this a
  // new match opens with every seat drawing its own capital as unknown, which reads as a bug.
  ObserveAndRemember(_outState, _rules);

  return Validate(_outState, _rules);
}

std::uint32_t GenerateGalaxyWithRetries(std::uint64_t _seed, SeatId _seatCount, const Rules& _rules, std::uint32_t _attempts,
                                        MatchState& _outState)
{
  for (std::uint32_t attempt = 0; attempt < _attempts; ++attempt)
  {
    if (GenerateGalaxy(_seed + attempt, _seatCount, _rules, _outState) == GenerationResult::Accepted)
    {
      return attempt + 1;
    }
  }

  return 0;
}

std::string DescribeGalaxy(const MatchState& _state)
{
  static constexpr std::array<const char*, 4> KIND_NAMES = {"capital ", "cluster ", "frontier", "sealed  "};

  std::string text = std::format("galaxy seed {} -- {} seats, {} systems, {} lanes, ends at tick {}, region opens {}\n", _state.seed,
                                 _state.seats.size(), _state.systems.size(), _state.lanes.size(), _state.endTick, _state.sealedOpensTick);

  for (const System& system : _state.systems)
  {
    text += std::format("  system {:>3}  {}  at ({:>5},{:>5})  home {:>3}  owner {:>3}  yield {}\n", system.id,
                        KIND_NAMES[static_cast<std::size_t>(system.kind)], system.position.xUnits, system.position.yUnits,
                        system.homeSeat == NO_SEAT ? -1 : static_cast<std::int32_t>(system.homeSeat),
                        system.owner == NO_SEAT ? -1 : static_cast<std::int32_t>(system.owner), system.yieldPerTick);
  }

  for (const Lane& lane : _state.lanes)
  {
    text += std::format("  lane   {:>3}  {:>3} - {:>3}  {} ticks, length squared {}\n", lane.id, lane.endA, lane.endB, lane.costTicks,
                        LaneLengthSquaredUnits(_state, lane));
  }

  for (const Fleet& fleet : _state.fleets)
  {
    text += std::format("  fleet  {:>3}  seat {}  at system {}  strength {}{}\n", fleet.id, fleet.owner, fleet.atSystem, fleet.strength,
                        fleet.pinned ? "  pinned" : "");
  }

  return text;
}

} // namespace Frontier
