#include "pch.h"
#include "CppUnitTest.h"

#include "Galaxy.h"
#include "MatchState.h"
#include "Random.h"
#include "Rules.h"
#include "Visibility.h"
#include "World.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr Frontier::SeatId SEAT_COUNTS[] = {6, 8, 12};

/// CppUnitTest takes its failure messages wide, and every message here is built with std::format.
/// The galaxy is ASCII by construction -- system kinds, ids and numbers -- so widening character by
/// character is exact rather than a transcoding that could be wrong.
std::wstring ToWide(const std::string& _text)
{
  return std::wstring{_text.begin(), _text.end()};
}

/// Generates and asserts the seed was accepted, returning the galaxy. Most tests below want a
/// galaxy rather than a result, and a rejected seed in one of them is a failure of the test's
/// premise rather than the thing under test.
Frontier::MatchState AcceptedGalaxy(std::uint64_t _seed, Frontier::SeatId _seats)
{
  Frontier::MatchState state;
  const Frontier::GenerationResult result = Frontier::GenerateGalaxy(_seed, _seats, Frontier::DEFAULT_RULES, state);
  Assert::AreEqual(static_cast<int>(Frontier::GenerationResult::Accepted), static_cast<int>(result),
                   ToWide(std::format("seed {} at {} seats: {}", _seed, static_cast<int>(_seats), Frontier::Describe(result))).c_str());
  return state;
}

/// Serializes the parts of a state that a resolver could observe, so two states can be compared for
/// being the same rather than merely for agreeing on a field somebody thought to check.
std::string Fingerprint(const Frontier::MatchState& _state)
{
  std::string text = std::format("{}|{}|{}|{}|", _state.seed, _state.tick, _state.endTick, _state.sealedOpensTick);
  for (const Frontier::System& system : _state.systems)
  {
    text += std::format("S{},{},{},{},{},{},{};", system.id, system.position.xUnits, system.position.yUnits, static_cast<int>(system.kind),
                        static_cast<int>(system.homeSeat), static_cast<int>(system.owner), system.yieldPerTick);
  }
  for (const Frontier::Lane& lane : _state.lanes)
  {
    text += std::format("L{},{},{},{};", lane.id, lane.endA, lane.endB, lane.costTicks);
  }
  for (const Frontier::Fleet& fleet : _state.fleets)
  {
    text += std::format("F{},{},{},{},{};", fleet.id, static_cast<int>(fleet.owner), fleet.atSystem, fleet.strength, fleet.pinned ? 1 : 0);
  }
  for (const Frontier::Seat& seat : _state.seats)
  {
    text += std::format("T{},{},{};", static_cast<int>(seat.id), seat.capital, seat.income);
  }
  return text;
}

} // namespace

// The random source. Everything the generator produces rests on this being the same sequence
// everywhere, which is the argument ADR-019 makes for it being ours rather than the standard
// library's.
TEST_CLASS(RandomTests)
{
public:
  TEST_METHOD(TheSameSeedGivesTheSameSequence)
  {
    Frontier::Random first{12345};
    Frontier::Random second{12345};
    for (int draw = 0; draw < 64; ++draw)
    {
      Assert::AreEqual(first.Next(), second.Next());
    }
  }

  TEST_METHOD(DifferentSeedsDiverge)
  {
    Frontier::Random first{1};
    Frontier::Random second{2};
    bool sawDifference = false;
    for (int draw = 0; draw < 8 && !sawDifference; ++draw)
    {
      sawDifference = first.Next() != second.Next();
    }
    Assert::IsTrue(sawDifference, L"Two seeds one apart produced the same first eight words.");
  }

  TEST_METHOD(TheFirstWordsAreTheKnownSplitMix64Values)
  {
    // Pinned, not derived: these are what SplitMix64 emits from seed 0, and a change to the mixing
    // constants would silently regenerate every galaxy in every stored match (ADR-012).
    Frontier::Random random{0};
    Assert::AreEqual(0xE220A8397B1DCDAFULL, random.Next());
    Assert::AreEqual(0x6E789E6AA1B965F4ULL, random.Next());
    Assert::AreEqual(0x06C45D188009454FULL, random.Next());
  }

  TEST_METHOD(BetweenStaysInRangeAndIsInclusive)
  {
    Frontier::Random random{7};
    bool sawLow = false;
    bool sawHigh = false;
    for (int draw = 0; draw < 2000; ++draw)
    {
      const std::int32_t value = random.Between(-2, 2);
      Assert::IsTrue(value >= -2 && value <= 2);
      sawLow = sawLow || value == -2;
      sawHigh = sawHigh || value == 2;
    }
    Assert::IsTrue(sawLow && sawHigh, L"Between never produced one of its endpoints.");
  }

  TEST_METHOD(BetweenWithOneValueReturnsIt)
  {
    Frontier::Random random{7};
    Assert::AreEqual(5, random.Between(5, 5));
    Assert::AreEqual(5, random.Between(5, 4));
  }
};

// The graph queries the generator and the resolver both rest on.
TEST_CLASS(GraphTests)
{
public:
  TEST_METHOD(IdsAreIndices)
  {
    // The whole state assumes it, so it is asserted rather than commented.
    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      const Frontier::MatchState state = AcceptedGalaxy(1, seats);
      for (std::size_t index = 0; index < state.systems.size(); ++index)
      {
        Assert::AreEqual(index, static_cast<std::size_t>(state.systems[index].id));
      }
      for (std::size_t index = 0; index < state.lanes.size(); ++index)
      {
        Assert::AreEqual(index, static_cast<std::size_t>(state.lanes[index].id));
      }
      for (std::size_t index = 0; index < state.fleets.size(); ++index)
      {
        Assert::AreEqual(index, static_cast<std::size_t>(state.fleets[index].id));
      }
      for (std::size_t index = 0; index < state.seats.size(); ++index)
      {
        Assert::AreEqual(index, static_cast<std::size_t>(state.seats[index].id));
      }
    }
  }

  TEST_METHOD(DistanceSquaredIsExactForKnownTriangles)
  {
    Assert::AreEqual(25LL, Frontier::DistanceSquaredUnits({0, 0}, {3, 4}));
    Assert::AreEqual(25LL, Frontier::DistanceSquaredUnits({-3, -4}, {0, 0}));
    Assert::AreEqual(0LL, Frontier::DistanceSquaredUnits({7, -7}, {7, -7}));
  }

  TEST_METHOD(ShortestPathCountsLaneCostsAndNotLanes)
  {
    // A chain of three one-tick lanes must beat a single four-tick lane, which is the whole point of
    // authored distance: hops are not the measure, ticks are.
    Frontier::MatchState state;
    for (int index = 0; index < 4; ++index)
    {
      state.systems.push_back(Frontier::System{.id = static_cast<Frontier::SystemId>(index),
                                               .position = {index, 0},
                                               .kind = Frontier::SystemKind::Frontier,
                                               .homeSeat = Frontier::NO_SEAT,
                                               .owner = Frontier::NO_SEAT,
                                               .yieldPerTick = 1});
    }
    state.lanes.push_back(Frontier::Lane{.id = 0, .endA = 0, .endB = 1, .costTicks = 1});
    state.lanes.push_back(Frontier::Lane{.id = 1, .endA = 1, .endB = 2, .costTicks = 1});
    state.lanes.push_back(Frontier::Lane{.id = 2, .endA = 2, .endB = 3, .costTicks = 1});
    state.lanes.push_back(Frontier::Lane{.id = 3, .endA = 0, .endB = 3, .costTicks = 4});

    const std::vector<std::int32_t> cost = Frontier::ShortestPathTicksFrom(state, 0);
    Assert::AreEqual(0, cost[0]);
    Assert::AreEqual(1, cost[1]);
    Assert::AreEqual(2, cost[2]);
    Assert::AreEqual(3, cost[3]);
  }

  TEST_METHOD(AnUnreachableSystemComesBackAsMinusOne)
  {
    Frontier::MatchState state;
    for (int index = 0; index < 3; ++index)
    {
      state.systems.push_back(Frontier::System{.id = static_cast<Frontier::SystemId>(index),
                                               .position = {index, 0},
                                               .kind = Frontier::SystemKind::Frontier,
                                               .homeSeat = Frontier::NO_SEAT,
                                               .owner = Frontier::NO_SEAT,
                                               .yieldPerTick = 1});
    }
    state.lanes.push_back(Frontier::Lane{.id = 0, .endA = 0, .endB = 1, .costTicks = 2});

    const std::vector<std::int32_t> cost = Frontier::ShortestPathTicksFrom(state, 0);
    Assert::AreEqual(0, cost[0]);
    Assert::AreEqual(2, cost[1]);
    Assert::AreEqual(-1, cost[2]);
  }

  TEST_METHOD(AdjacencyIsSymmetric)
  {
    const Frontier::MatchState state = AcceptedGalaxy(3, 8);
    for (const Frontier::Lane& lane : state.lanes)
    {
      Assert::IsTrue(Frontier::AreAdjacent(state, lane.endA, lane.endB));
      Assert::IsTrue(Frontier::AreAdjacent(state, lane.endB, lane.endA));
    }
  }
};

// The generator, against every guarantee the one-pager and ADR-014 state.
TEST_CLASS(GalaxyGeneratorTests)
{
public:
  TEST_METHOD(ProducesACapitalAndAPinnedGarrisonPerSeat)
  {
    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      const Frontier::MatchState state = AcceptedGalaxy(1, seats);
      Assert::AreEqual(static_cast<std::size_t>(seats), state.seats.size());

      std::size_t capitals = 0;
      for (const Frontier::System& system : state.systems)
      {
        capitals += system.kind == Frontier::SystemKind::Capital ? 1 : 0;
      }
      Assert::AreEqual(static_cast<std::size_t>(seats), capitals);

      for (const Frontier::Seat& seat : state.seats)
      {
        const Frontier::System& capital = state.systems[seat.capital];
        Assert::AreEqual(static_cast<int>(Frontier::SystemKind::Capital), static_cast<int>(capital.kind));
        Assert::AreEqual(static_cast<int>(seat.id), static_cast<int>(capital.owner));

        // ADR-018: the garrison is a pinned fleet, not a defence value on the system.
        std::size_t pinnedHere = 0;
        for (const Frontier::Fleet& fleet : state.fleets)
        {
          if (fleet.owner == seat.id && fleet.atSystem == seat.capital && fleet.pinned)
          {
            ++pinnedHere;
            Assert::AreEqual(Frontier::DEFAULT_RULES.startingGarrisonStrength, fleet.strength);
            Assert::AreEqual(static_cast<int>(Frontier::NO_LANE), static_cast<int>(fleet.onLane));
          }
        }
        Assert::AreEqual(static_cast<std::size_t>(1), pinnedHere);
      }
    }
  }

  TEST_METHOD(EveryCapitalHasARivalCapitalWithinThreeTicks)
  {
    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      for (std::uint64_t seed = 1; seed <= 40; ++seed)
      {
        const Frontier::MatchState state = AcceptedGalaxy(seed, seats);
        for (const Frontier::Seat& seat : state.seats)
        {
          const std::vector<std::int32_t> reach = Frontier::ShortestPathTicksFrom(state, seat.capital);
          std::int32_t nearest = -1;
          for (const Frontier::Seat& other : state.seats)
          {
            if (other.id != seat.id && reach[other.capital] >= 0 && (nearest < 0 || reach[other.capital] < nearest))
            {
              nearest = reach[other.capital];
            }
          }
          Assert::IsTrue(nearest >= 0 && nearest <= Frontier::DEFAULT_RULES.capitalRivalMaxTicks,
                         ToWide(std::format("seed {} seats {} seat {}: nearest rival {} ticks", seed, static_cast<int>(seats),
                                            static_cast<int>(seat.id), nearest))
                           .c_str());
        }
      }
    }
  }

  TEST_METHOD(ClusterLanesAreOneTickAndEverythingElseIsTwoToFour)
  {
    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      for (std::uint64_t seed = 1; seed <= 40; ++seed)
      {
        const Frontier::MatchState state = AcceptedGalaxy(seed, seats);
        for (const Frontier::Lane& lane : state.lanes)
        {
          const Frontier::System& endA = state.systems[lane.endA];
          const Frontier::System& endB = state.systems[lane.endB];
          const bool intra = endA.homeSeat != Frontier::NO_SEAT && endA.homeSeat == endB.homeSeat;

          if (intra)
          {
            Assert::AreEqual(1, lane.costTicks, L"A lane inside a starting cluster must be one tick.");
          }
          else
          {
            Assert::IsTrue(lane.costTicks >= 2 && lane.costTicks <= 4,
                           ToWide(std::format("lane {} costs {} ticks outside a cluster", lane.id, lane.costTicks)).c_str());
          }
        }
      }
    }
  }

  TEST_METHOD(DrawnLengthIsMonotoneInTickCost)
  {
    // ADR-014's constraint, walked over every pair of lanes rather than sampled. This is the
    // property that stops the map lying about travel time, so it is checked exhaustively.
    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      for (std::uint64_t seed = 1; seed <= 20; ++seed)
      {
        const Frontier::MatchState state = AcceptedGalaxy(seed, seats);
        for (const Frontier::Lane& first : state.lanes)
        {
          for (const Frontier::Lane& second : state.lanes)
          {
            if (first.costTicks < second.costTicks)
            {
              Assert::IsTrue(Frontier::LaneLengthSquaredUnits(state, first) <= Frontier::LaneLengthSquaredUnits(state, second),
                             ToWide(std::format("lane {} costs {} but is drawn longer than lane {} at {}", first.id, first.costTicks,
                                                second.id, second.costTicks))
                               .c_str());
            }
          }
        }
      }
    }
  }

  TEST_METHOD(NoTwoSystemsAreCloserThanTheMinimumSeparation)
  {
    const std::int64_t minimumSquared =
      static_cast<std::int64_t>(Frontier::DEFAULT_RULES.minSeparationUnits) * Frontier::DEFAULT_RULES.minSeparationUnits;
    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      for (std::uint64_t seed = 1; seed <= 20; ++seed)
      {
        const Frontier::MatchState state = AcceptedGalaxy(seed, seats);
        for (std::size_t a = 0; a < state.systems.size(); ++a)
        {
          for (std::size_t b = a + 1; b < state.systems.size(); ++b)
          {
            Assert::IsTrue(Frontier::DistanceSquaredUnits(state.systems[a].position, state.systems[b].position) >= minimumSquared,
                           ToWide(std::format("systems {} and {} are too close", a, b)).c_str());
          }
        }
      }
    }
  }

  TEST_METHOD(TheGalaxyIsConnected)
  {
    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      for (std::uint64_t seed = 1; seed <= 20; ++seed)
      {
        const Frontier::MatchState state = AcceptedGalaxy(seed, seats);
        const std::vector<std::int32_t> reach = Frontier::ShortestPathTicksFrom(state, state.seats[0].capital);
        Assert::IsTrue(std::ranges::find(reach, -1) == reach.end(), L"A system cannot be reached from a capital.");
      }
    }
  }

  TEST_METHOD(TheSealedRegionIsGeneratedVisibleAndReachable)
  {
    // The one-pager makes the region visible from tick one and openable mid-match. Nothing in
    // MVP-02 can enter it; what this asserts is that it exists, is on the map, and is not stranded.
    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      const Frontier::MatchState state = AcceptedGalaxy(1, seats);
      std::size_t sites = 0;
      for (const Frontier::System& system : state.systems)
      {
        if (system.kind == Frontier::SystemKind::Sealed)
        {
          ++sites;
          Assert::AreEqual(static_cast<int>(Frontier::NO_SEAT), static_cast<int>(system.owner));
        }
      }
      Assert::AreEqual(static_cast<std::size_t>(Frontier::DEFAULT_RULES.sealedSystemCount), sites);

      Assert::IsTrue(state.sealedOpensTick > 0 && state.sealedOpensTick < state.endTick,
                     L"The region must open during the match, on a tick known from the start.");
    }
  }

  TEST_METHOD(TheFrontierIsRicherThanHome)
  {
    // "Near and safe, far and rich" is the first of the three interesting decisions, and it is only
    // a decision if the frontier actually pays more.
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    std::int32_t clusterBest = 0;
    std::int32_t frontierWorst = 1000;
    for (const Frontier::System& system : state.systems)
    {
      if (system.kind == Frontier::SystemKind::Cluster)
      {
        clusterBest = std::max(clusterBest, system.yieldPerTick);
      }
      if (system.kind == Frontier::SystemKind::Frontier)
      {
        frontierWorst = std::min(frontierWorst, system.yieldPerTick);
      }
    }
    Assert::IsTrue(frontierWorst > clusterBest, L"The poorest frontier system must out-yield the richest home system.");
  }

  TEST_METHOD(TheSameSeedGivesTheSameGalaxy)
  {
    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      const Frontier::MatchState first = AcceptedGalaxy(99, seats);
      const Frontier::MatchState second = AcceptedGalaxy(99, seats);
      Assert::AreEqual(Fingerprint(first), Fingerprint(second));
    }
  }

  TEST_METHOD(DifferentSeedsGiveDifferentGalaxies)
  {
    // A generator whose output does not depend on its seed is a generator with one galaxy in it.
    const Frontier::MatchState first = AcceptedGalaxy(1, 8);
    const Frontier::MatchState second = AcceptedGalaxy(2, 8);
    Assert::AreNotEqual(Fingerprint(first), Fingerprint(second));
  }

  TEST_METHOD(TooFewSeatsIsRefusedRatherThanCrashing)
  {
    Frontier::MatchState state;
    Assert::AreNotEqual(static_cast<int>(Frontier::GenerationResult::Accepted),
                        static_cast<int>(Frontier::GenerateGalaxy(1, 1, Frontier::DEFAULT_RULES, state)));
  }

  TEST_METHOD(ImpossibleRulesAreRejectedAndNamed)
  {
    // The rejection path has to work, or the measurement below is measuring nothing. A minimum
    // separation wider than the whole galaxy cannot be satisfied by any seed.
    Frontier::Rules rules = Frontier::DEFAULT_RULES;
    rules.minSeparationUnits = 500;

    Frontier::MatchState state;
    const Frontier::GenerationResult result = Frontier::GenerateGalaxy(1, 8, rules, state);
    Assert::AreEqual(static_cast<int>(Frontier::GenerationResult::SeparationTooSmall), static_cast<int>(result));
    Assert::AreEqual(static_cast<std::size_t>(0), std::string{Frontier::Describe(result)}.find("two systems closer"));
  }

  TEST_METHOD(RetryingReportsHowManySeedsItTook)
  {
    Frontier::MatchState state;
    Assert::AreEqual(1U, Frontier::GenerateGalaxyWithRetries(1, 8, Frontier::DEFAULT_RULES, 8, state));

    Frontier::Rules impossible = Frontier::DEFAULT_RULES;
    impossible.minSeparationUnits = 500;
    Assert::AreEqual(0U, Frontier::GenerateGalaxyWithRetries(1, 8, impossible, 4, state));
  }

  // The figure ADR-014 asks for, measured rather than asserted: how often a seed is refused.
  //
  // It is a test so that it cannot rot, and its assertion is deliberately loose -- a rate under a
  // fifth means match creation is a single attempt in practice. The number itself goes in the
  // slice report, and the log line below is where it comes from.
  TEST_METHOD(TheRejectionRateIsLowAtEverySeatCount)
  {
    constexpr std::uint64_t SEEDS = 1000;

    for (const Frontier::SeatId seats : SEAT_COUNTS)
    {
      std::map<int, std::uint64_t> refusals;
      std::uint64_t rejected = 0;
      Frontier::MatchState state;

      for (std::uint64_t seed = 0; seed < SEEDS; ++seed)
      {
        const Frontier::GenerationResult result = Frontier::GenerateGalaxy(seed, seats, Frontier::DEFAULT_RULES, state);
        if (result != Frontier::GenerationResult::Accepted)
        {
          ++rejected;
          ++refusals[static_cast<int>(result)];
        }
      }

      std::string detail;
      for (const auto& [reason, count] : refusals)
      {
        detail += std::format(" {}x{}", count, Frontier::Describe(static_cast<Frontier::GenerationResult>(reason)));
      }
      Logger::WriteMessage(ToWide(std::format("seats {:>2}: {} of {} seeds rejected;{}\n", static_cast<int>(seats), rejected, SEEDS,
                                              detail.empty() ? " none" : detail))
                             .c_str());

      Assert::IsTrue(rejected * 5 < SEEDS, ToWide(std::format("seats {}: {} of {} seeds rejected, which is too many to call a safety net",
                                                              static_cast<int>(seats), rejected, SEEDS))
                                             .c_str());
    }
  }

  /// Not an assertion: a galaxy printed for a person to read. Slice 2 gives it a picture; until
  /// then this is the only way to look at what the generator built.
  TEST_METHOD(LogsAGalaxyForHumanEyes)
  {
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    Logger::WriteMessage(ToWide(Frontier::DescribeGalaxy(state)).c_str());

    std::map<int, std::size_t> costs;
    for (const Frontier::Lane& lane : state.lanes)
    {
      ++costs[lane.costTicks];
    }
    std::string histogram;
    for (const auto& [cost, count] : costs)
    {
      histogram += std::format(" {} lanes at {} ticks;", count, cost);
    }

    std::int32_t lowX = 0;
    std::int32_t highX = 0;
    std::int32_t lowY = 0;
    std::int32_t highY = 0;
    for (const Frontier::System& system : state.systems)
    {
      lowX = std::min(lowX, system.position.xUnits);
      highX = std::max(highX, system.position.xUnits);
      lowY = std::min(lowY, system.position.yUnits);
      highY = std::max(highY, system.position.yUnits);
    }

    // The extent is what slice 2's legibility measurement starts from: ADR-003 projects a map unit
    // onto eight pixels at the default zoom, so this says how many 640x400 screens the galaxy is.
    Logger::WriteMessage(ToWide(std::format("extent x[{},{}] y[{},{}];{}\n", lowX, highX, lowY, highY, histogram)).c_str());
    Assert::IsTrue(highX > lowX && highY > lowY);
  }
};

// The thin owner, and the seam it still satisfies.
TEST_CLASS(WorldTests)
{
public:
  TEST_METHOD(ADefaultWorldIsARealAcceptedMatch)
  {
    const Frontier::World world;
    Assert::AreEqual(static_cast<int>(Frontier::GenerationResult::Accepted), static_cast<int>(world.Generation()));
    Assert::AreEqual(static_cast<std::size_t>(Frontier::World::DEFAULT_SEAT_COUNT), world.State().seats.size());
    Assert::AreEqual(Frontier::World::DEFAULT_SEED, world.State().seed);
  }

  TEST_METHOD(CountsItsTicks)
  {
    Frontier::World world;
    Assert::AreEqual(0ULL, world.TickCount());
    for (int tick = 0; tick < 5; ++tick)
    {
      world.Tick();
    }
    Assert::AreEqual(5ULL, world.TickCount());
    Assert::AreEqual(5ULL, world.State().tick);
  }

  TEST_METHOD(TheSnapshotCarriesTheTick)
  {
    // All this asserts is the transitional behavior World.h describes: there is no ship, so the
    // MVP-01 record carries the clock and nothing else. Slice 2 deletes both.
    Frontier::World world;
    world.Tick();
    world.Tick();

    const Neuron::ShipState state = world.Snapshot();
    Assert::AreEqual(2ULL, state.tick);
    Assert::AreEqual(0LL, state.positionXMillimetres);
    Assert::AreEqual(0LL, state.positionZMillimetres);
  }

  TEST_METHOD(AnOrderChangesNothing)
  {
    Frontier::World world;
    const std::string before = Fingerprint(world.State());
    world.ApplyOrder(Neuron::MoveToOrder{.targetXMillimetres = 60000, .targetZMillimetres = 30000});
    Assert::AreEqual(before, Fingerprint(world.State()));
  }

  TEST_METHOD(TickingDoesNotDisturbTheGalaxy)
  {
    // The tick is the clock and nothing else until the resolver lands. If this starts failing, a
    // phase was added without the plan.
    Frontier::World world;
    Frontier::MatchState before = world.State();
    for (int tick = 0; tick < 20; ++tick)
    {
      world.Tick();
    }
    before.tick = world.State().tick;
    Assert::AreEqual(Fingerprint(before), Fingerprint(world.State()));
  }

  TEST_METHOD(ARequestedSeedAndSeatCountAreHonored)
  {
    const Frontier::World world{7, 6, Frontier::DEFAULT_RULES};
    Assert::AreEqual(static_cast<int>(Frontier::GenerationResult::Accepted), static_cast<int>(world.Generation()));
    Assert::AreEqual(7ULL, world.State().seed);
    Assert::AreEqual(static_cast<std::size_t>(6), world.State().seats.size());
  }
};

// The fog rule (ADR-017) and the per-seat snapshot (ADR-005). These are the tests that matter most
// for trust rather than for correctness: the filter is the only thing deciding what a seat may see,
// so most of what follows asserts an ABSENCE.
TEST_CLASS(VisibilityTests)
{
public:
  TEST_METHOD(ASeatObservesItsOwnCapitalFromTickOne)
  {
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    for (const Frontier::Seat& seat : state.seats)
    {
      const std::vector<bool> observed = Frontier::ObservedSystems(state, Frontier::DEFAULT_RULES, seat.id);
      Assert::IsTrue(observed[seat.capital], L"a seat must see the system its garrison is standing in");
    }
  }

  TEST_METHOD(ObservationReachesOneLaneByDefault)
  {
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Frontier::Seat& seat = state.seats[0];
    const std::vector<bool> observed = Frontier::ObservedSystems(state, Frontier::DEFAULT_RULES, seat.id);

    for (const Frontier::Lane& lane : state.lanes)
    {
      if (lane.endA == seat.capital)
      {
        Assert::IsTrue(observed[lane.endB], L"a neighbor of the capital is within the default reach");
      }
      if (lane.endB == seat.capital)
      {
        Assert::IsTrue(observed[lane.endA]);
      }
    }
  }

  TEST_METHOD(AReachOfZeroSeesOnlyWhereTheFleetStands)
  {
    Frontier::Rules rules = Frontier::DEFAULT_RULES;
    rules.scoutingRevealLanes = 0;

    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Frontier::Seat& seat = state.seats[0];
    const std::vector<bool> observed = Frontier::ObservedSystems(state, rules, seat.id);

    std::size_t count = 0;
    for (const bool seen : observed)
    {
      count += seen ? 1 : 0;
    }
    Assert::AreEqual(static_cast<std::size_t>(1), count);
    Assert::IsTrue(observed[seat.capital]);
  }

  TEST_METHOD(AFleetInTransitObservesNothing)
  {
    Frontier::MatchState state = AcceptedGalaxy(1, 8);

    // Put every one of seat 0's fleets on a lane. A fleet between systems is not at one, and a
    // scout reporting from halfway down a lane would weaken the one-pager's tick resolution.
    for (Frontier::Fleet& fleet : state.fleets)
    {
      if (fleet.owner == 0)
      {
        fleet.atSystem = Frontier::NO_SYSTEM;
        fleet.onLane = 0;
        fleet.towardSystem = state.lanes[0].endB;
      }
    }

    const std::vector<bool> observed = Frontier::ObservedSystems(state, Frontier::DEFAULT_RULES, 0);
    for (const bool seen : observed)
    {
      Assert::IsFalse(seen, L"a fleet in transit observes nothing");
    }
  }

  TEST_METHOD(TheTopologyIsPublicInFull)
  {
    // Every system and every lane, at their authored coordinates and costs, whatever the seat has
    // observed. This is what makes the map worth drawing from tick one.
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Neuron::VisibleSnapshot snapshot = Frontier::VisibleSnapshotFor(state, Frontier::DEFAULT_RULES, 0);

    Assert::AreEqual(state.systems.size(), snapshot.systems.size());
    Assert::AreEqual(state.lanes.size(), snapshot.lanes.size());
    for (std::size_t index = 0; index < state.systems.size(); ++index)
    {
      Assert::AreEqual(state.systems[index].position.xUnits, snapshot.systems[index].xUnits);
      Assert::AreEqual(state.systems[index].position.yUnits, snapshot.systems[index].yUnits);
    }
    for (std::size_t index = 0; index < state.lanes.size(); ++index)
    {
      Assert::AreEqual(state.lanes[index].costTicks, snapshot.lanes[index].costTicks);
    }
  }

  TEST_METHOD(AnUnobservedSystemCarriesNoContentsAtAll)
  {
    // The leak test. Whatever a seat has not seen, it is told nothing about -- not a default, not
    // a stale guess, nothing.
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Neuron::VisibleSnapshot snapshot = Frontier::VisibleSnapshotFor(state, Frontier::DEFAULT_RULES, 0);

    std::size_t unknown = 0;
    for (const Neuron::SystemView& view : snapshot.systems)
    {
      if (view.visibility != Neuron::Visibility::Unknown)
      {
        continue;
      }
      ++unknown;
      Assert::AreEqual(static_cast<int>(Frontier::NO_SEAT), static_cast<int>(view.owner));
      Assert::AreEqual(0, view.yieldPerTick);
      Assert::AreEqual(0, view.garrisonStrength);
      Assert::AreEqual(0ULL, view.observedTick);
    }
    Assert::IsTrue(unknown > 0, L"an eight-seat galaxy must have somewhere seat 0 has not been");
  }

  TEST_METHOD(LeavingASystemTurnsObservedIntoRememberedWithTheOldTick)
  {
    Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Frontier::SystemId capital = state.seats[0].capital;
    const std::int32_t knownYield = state.systems[capital].yieldPerTick;

    // The fleets go, the clock moves on, and observation runs again. The capital's contents must
    // now read as what was seen at tick 0, not as what is true at tick 5.
    state.fleets.clear();
    state.tick = 5;
    Frontier::ObserveAndRemember(state, Frontier::DEFAULT_RULES);

    const Neuron::VisibleSnapshot snapshot = Frontier::VisibleSnapshotFor(state, Frontier::DEFAULT_RULES, 0);
    const Neuron::SystemView& view = snapshot.systems[capital];

    Assert::AreEqual(static_cast<int>(Neuron::Visibility::Remembered), static_cast<int>(view.visibility));
    Assert::AreEqual(knownYield, view.yieldPerTick);
    Assert::AreEqual(0ULL, view.observedTick, L"the stamp is the tick it was seen, not the tick it is now");
  }

  TEST_METHOD(FogDoesNotCloseAgain)
  {
    Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Frontier::SystemId capital = state.seats[0].capital;

    state.fleets.clear();
    for (int tick = 0; tick < 10; ++tick)
    {
      state.tick = static_cast<std::uint64_t>(tick);
      Frontier::ObserveAndRemember(state, Frontier::DEFAULT_RULES);
    }

    Assert::IsTrue(state.seats[0].memory[capital].known, L"a seat that has seen a system knows it is there");
  }

  TEST_METHOD(AFleetInTransitIsPublicToEverySeat)
  {
    Frontier::MatchState state = AcceptedGalaxy(1, 8);
    Frontier::Fleet& fleet = state.fleets[0];
    const Frontier::SeatId owner = fleet.owner;
    fleet.atSystem = Frontier::NO_SYSTEM;
    fleet.onLane = 3;
    fleet.towardSystem = state.lanes[3].endB;
    fleet.departedTick = 2;
    fleet.arrivesTick = 5;

    for (const Frontier::Seat& seat : state.seats)
    {
      const Neuron::VisibleSnapshot snapshot = Frontier::VisibleSnapshotFor(state, Frontier::DEFAULT_RULES, seat.id);
      Assert::AreEqual(static_cast<std::size_t>(1), snapshot.transits.size(),
                       L"a departed fleet is visible in transit to everyone, with its tick-ETA");
      Assert::AreEqual(static_cast<int>(owner), static_cast<int>(snapshot.transits[0].owner));
      Assert::AreEqual(5ULL, snapshot.transits[0].arrivesTick);
    }
  }

  TEST_METHOD(AFleetAtASystemIsNotATransit)
  {
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Neuron::VisibleSnapshot snapshot = Frontier::VisibleSnapshotFor(state, Frontier::DEFAULT_RULES, 0);
    Assert::IsTrue(snapshot.transits.empty(), L"nothing has departed yet");
  }

  TEST_METHOD(TwoSeatsAreToldDifferentThings)
  {
    // If this ever fails, the filter has stopped filtering.
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Neuron::VisibleSnapshot first = Frontier::VisibleSnapshotFor(state, Frontier::DEFAULT_RULES, 0);
    const Neuron::VisibleSnapshot second = Frontier::VisibleSnapshotFor(state, Frontier::DEFAULT_RULES, 4);

    bool sawDifference = false;
    for (std::size_t index = 0; index < first.systems.size() && !sawDifference; ++index)
    {
      sawDifference = first.systems[index].visibility != second.systems[index].visibility;
    }
    Assert::IsTrue(sawDifference, L"two seats on opposite sides of the galaxy must not see the same thing");
  }

  TEST_METHOD(TheSnapshotCarriesTheCountdownsTheDesignMakesPublic)
  {
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Neuron::VisibleSnapshot snapshot = Frontier::VisibleSnapshotFor(state, Frontier::DEFAULT_RULES, 0);

    Assert::AreEqual(state.endTick, snapshot.endTick);
    Assert::AreEqual(state.sealedOpensTick, snapshot.sealedOpensTick);
    Assert::AreEqual(state.seats.size(), snapshot.seats.size());
    for (const Neuron::SeatView& seat : snapshot.seats)
    {
      Assert::AreEqual(Frontier::DEFAULT_RULES.capitalGuardTicks, seat.capitalGuardEndsTick);
    }
  }

  TEST_METHOD(ASnapshotSurvivesTheWire)
  {
    // The filter's output has to be the thing that actually crosses a transport, so the two are
    // tested together at least once rather than only in their own suites.
    const Frontier::MatchState state = AcceptedGalaxy(1, 8);
    const Neuron::VisibleSnapshot original = Frontier::VisibleSnapshotFor(state, Frontier::DEFAULT_RULES, 0);

    std::vector<std::byte> bytes;
    Neuron::Serialize(original, bytes);

    Neuron::VisibleSnapshot restored;
    Assert::IsTrue(Neuron::DeserializeVisibleSnapshot(bytes, restored));
    Assert::AreEqual(original.systems.size(), restored.systems.size());
    for (std::size_t index = 0; index < original.systems.size(); ++index)
    {
      Assert::AreEqual(static_cast<int>(original.systems[index].visibility), static_cast<int>(restored.systems[index].visibility));
      Assert::AreEqual(original.systems[index].yieldPerTick, restored.systems[index].yieldPerTick);
    }
  }
};

} // namespace GameLogicTests
