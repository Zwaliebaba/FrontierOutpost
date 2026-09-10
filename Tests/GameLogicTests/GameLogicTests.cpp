#include "pch.h"
#include "CppUnitTest.h"

#include "GalaxyGenerator.h"

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// The seeds every constraint below is checked against.
///
/// Fixed rather than drawn, because a suite that picks its own seeds fails on a machine you cannot
/// reach and passes when you rerun it. Spread across the range so they exercise different jitter
/// and different name orders rather than neighbouring states of the same one.
constexpr std::array<std::uint64_t, 8> SEEDS = {
  0ULL, 1ULL, 42ULL, 0x0123456789ABCDEFULL, 0xFFFFFFFFFFFFFFFFULL, 0xDEADBEEFULL, 7919ULL, 0x8000000000000000ULL,
};

/// A hash of everything a galaxy IS, for the determinism check.
///
/// FNV-1a over the fields in index order. It covers the positions as well as the graph: the galaxy
/// is what the player sees as much as what a fleet moves through, so a layout that shifted by one
/// unit is a layout that changed.
[[nodiscard]] std::uint64_t HashGalaxy(const Frontier::Galaxy& _galaxy)
{
  std::uint64_t hash = 0xCBF29CE484222325ULL;
  const auto absorb = [&hash](std::uint64_t _value)
  {
    for (std::int32_t byte = 0; byte < 8; ++byte)
    {
      hash ^= (_value >> (byte * 8)) & 0xFFULL;
      hash *= 0x100000001B3ULL;
    }
  };

  for (const Frontier::GalaxySystem& system : _galaxy.Systems())
  {
    for (const char letter : system.name)
    {
      absorb(static_cast<std::uint64_t>(static_cast<unsigned char>(letter)));
    }
    absorb(static_cast<std::uint64_t>(system.kind));
    absorb(static_cast<std::uint64_t>(system.owner.Index()));
    absorb(static_cast<std::uint64_t>(system.startingCluster.Index()));
    absorb(static_cast<std::uint64_t>(system.positionX));
    absorb(static_cast<std::uint64_t>(system.positionY));
  }

  for (const Frontier::GalaxyLane& lane : _galaxy.Lanes())
  {
    absorb(static_cast<std::uint64_t>(lane.a.Index()));
    absorb(static_cast<std::uint64_t>(lane.b.Index()));
    absorb(lane.costTicks);
  }

  return hash;
}

[[nodiscard]] std::wstring Where(std::uint32_t _players, std::uint64_t _seed)
{
  return L"players " + std::to_wstring(_players) + L", seed " + std::to_wstring(_seed);
}

[[nodiscard]] Frontier::MatchRules RulesFor(std::uint32_t _players)
{
  Frontier::MatchRules rules;
  rules.playerCount = _players;
  return rules;
}

/// Generates and asserts the galaxy was accepted, so that a constraint test which fails does so on
/// the constraint it is named for rather than on an empty graph.
[[nodiscard]] Frontier::Galaxy GenerateOrFail(std::uint32_t _players, std::uint64_t _seed)
{
  Frontier::Galaxy galaxy;
  const Frontier::GalaxyRejection rejection = Frontier::GalaxyGenerator::TryGenerate(RulesFor(_players), _seed, galaxy);

  const std::string reason = Frontier::Describe(rejection);
  Assert::IsTrue(rejection == Frontier::GalaxyRejection::None,
                 (Where(_players, _seed) + L": " + std::wstring(reason.begin(), reason.end())).c_str());
  return galaxy;
}

} // namespace

// The promises the one-pager makes about the galaxy, checked on the finished graph.
//
// Every one of these is a sentence from `space-4x-one-pager-v10.md` turned into an assertion: a
// bounded galaxy, a rival capital within three ticks, one-tick lanes inside a starting cluster,
// two-to-four-tick lanes toward the frontier, a sealed region in the middle. They run across the
// whole supported player range because six players and twelve players are different rings, and a
// constraint that holds for one is not thereby true for the other.
TEST_CLASS(GalaxyGenerationTests)
{
public:
  TEST_METHOD(EveryPlayerCountAndSeedProducesAnAcceptedGalaxy)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      for (const std::uint64_t seed : SEEDS)
      {
        const Frontier::Galaxy galaxy = GenerateOrFail(players, seed);
        Assert::IsTrue(galaxy.SystemCount() > players, L"a galaxy is more than its capitals");
      }
    }
  }

  // How much galaxy there is, per player count.
  //
  // A count rather than a formula, because the count is what `Design/Reference/galaxy-generation.md`
  // records as fact and what a reader checks the document against. It also catches the quiet
  // failure `AddLane` is capable of: it refuses a duplicate by returning an invalid id, and every
  // caller in the generator discards that id, so a layout change that made two lanes collide would
  // silently produce a sparser galaxy and nothing else here would notice.
  TEST_METHOD(TheGalaxyIsTheSizeTheRulesAskFor)
  {
    struct Size
    {
      std::uint32_t players;
      std::uint32_t systems;
      std::uint32_t lanes;
    };

    constexpr std::array<Size, 7> SIZES = {{
      {6, 31, 45},
      {7, 36, 53},
      {8, 41, 60},
      {9, 46, 68},
      {10, 51, 75},
      {11, 56, 83},
      {12, 61, 90},
    }};

    for (const Size& size : SIZES)
    {
      for (const std::uint64_t seed : SEEDS)
      {
        const Frontier::Galaxy galaxy = GenerateOrFail(size.players, seed);
        Assert::AreEqual(size.systems, galaxy.SystemCount(), (Where(size.players, seed) + L": systems").c_str());
        Assert::AreEqual(size.lanes, galaxy.LaneCount(), (Where(size.players, seed) + L": lanes").c_str());
      }
    }
  }

  TEST_METHOD(TheGalaxyIsConnected)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      for (const std::uint64_t seed : SEEDS)
      {
        const Frontier::Galaxy galaxy = GenerateOrFail(players, seed);
        Assert::IsTrue(galaxy.IsConnected(), (Where(players, seed) + L": some system is cut off").c_str());
      }
    }
  }

  // "The generator guarantees each capital a rival capital within three ticks" -- and it is EACH,
  // so the check is per capital rather than over the galaxy as a whole. A layout in which one
  // player is a lonely outpost is the failure this rule exists to prevent.
  TEST_METHOD(EveryCapitalHasARivalWithinThreeTicks)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      const Frontier::MatchRules rules = RulesFor(players);

      for (const std::uint64_t seed : SEEDS)
      {
        const Frontier::Galaxy galaxy = GenerateOrFail(players, seed);

        const std::vector<Frontier::SystemId> capitals = galaxy.Capitals();
        Assert::AreEqual(static_cast<size_t>(players), capitals.size(), L"one capital per player");

        for (const Frontier::SystemId capital : capitals)
        {
          const std::vector<std::uint32_t> reach = galaxy.ShortestPathTicksFrom(capital);

          std::uint32_t nearest = Frontier::Galaxy::UNREACHABLE;
          for (const Frontier::SystemId rival : capitals)
          {
            if (rival != capital)
            {
              nearest = std::min(nearest, reach[rival.AsSize()]);
            }
          }

          Assert::IsTrue(nearest <= rules.maximumTicksToNearestRival,
                         (Where(players, seed) + L": nearest rival is " + std::to_wstring(nearest) + L" ticks away").c_str());
        }
      }
    }
  }

  // The two cost bands, read off the graph. A lane whose ends were generated into the same starting
  // cluster is inside a cluster and costs one; everything else leaves one and is a frontier lane.
  TEST_METHOD(LaneCostsMatchTheTwoBands)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      const Frontier::MatchRules rules = RulesFor(players);

      for (const std::uint64_t seed : SEEDS)
      {
        const Frontier::Galaxy galaxy = GenerateOrFail(players, seed);

        for (const Frontier::GalaxyLane& lane : galaxy.Lanes())
        {
          const Frontier::GalaxySystem& from = galaxy.SystemAt(lane.a);
          const Frontier::GalaxySystem& to = galaxy.SystemAt(lane.b);
          const bool insideOneCluster = from.startingCluster.IsValid() && from.startingCluster == to.startingCluster;

          const std::wstring context = Where(players, seed) + L": " + std::wstring(from.name.begin(), from.name.end()) + L" to " +
                                       std::wstring(to.name.begin(), to.name.end()) + L" costs " + std::to_wstring(lane.costTicks);

          if (insideOneCluster)
          {
            Assert::AreEqual(1U, lane.costTicks, (context + L", inside a cluster").c_str());
          }
          else
          {
            Assert::IsTrue(lane.costTicks >= rules.frontierLaneMinimumTicks && lane.costTicks <= rules.frontierLaneMaximumTicks,
                           (context + L", leaving a cluster").c_str());
          }
        }
      }
    }
  }

  // Visible from tick one. Placing it belongs to 4X-01 and opening it belongs to Phase 2, so what
  // is testable now is that it exists, is unowned, and can be got at from every seat.
  TEST_METHOD(TheSealedRegionIsPlacedAndReachable)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      for (const std::uint64_t seed : SEEDS)
      {
        const Frontier::Galaxy galaxy = GenerateOrFail(players, seed);

        const Frontier::SystemId region = galaxy.RegionAnchor();
        Assert::IsTrue(region.IsValid(), (Where(players, seed) + L": no region").c_str());
        Assert::IsFalse(galaxy.LanesAt(region).empty(), (Where(players, seed) + L": region has no lanes").c_str());
        Assert::IsFalse(galaxy.SystemAt(region).owner.IsValid(), L"the region can be raided, not claimed");

        for (const Frontier::SystemId capital : galaxy.Capitals())
        {
          Assert::IsTrue(galaxy.ShortestPathTicks(capital, region) != Frontier::Galaxy::UNREACHABLE,
                         (Where(players, seed) + L": a capital cannot reach the region").c_str());
        }
      }
    }
  }

  TEST_METHOD(EachPlayerStartsWithOneCapitalAndItsCluster)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      const Frontier::MatchRules rules = RulesFor(players);
      const Frontier::Galaxy galaxy = GenerateOrFail(players, 12345ULL);

      std::vector<std::uint32_t> capitalsOwned(players, 0);
      std::vector<std::uint32_t> satellitesInCluster(players, 0);

      for (const Frontier::GalaxySystem& system : galaxy.Systems())
      {
        if (system.kind == Frontier::SystemKind::Capital)
        {
          Assert::IsTrue(system.owner.IsValid(), L"a capital is somebody's seat");
          capitalsOwned[system.owner.AsSize()]++;
        }
        else
        {
          Assert::IsFalse(system.owner.IsValid(), L"nothing but a capital is owned at generation");
        }

        if (system.kind == Frontier::SystemKind::Satellite)
        {
          satellitesInCluster[system.startingCluster.AsSize()]++;
        }
      }

      for (std::uint32_t player = 0; player < players; ++player)
      {
        Assert::AreEqual(1U, capitalsOwned[player], L"exactly one capital per player");
        Assert::AreEqual(rules.satellitesPerCapital, satellitesInCluster[player], L"every cluster is the same size");
      }
    }
  }

  // Sixty systems drawn from sixty-four names, without replacement. Two systems sharing a name is a
  // map two players cannot talk about over a shared link.
  TEST_METHOD(SystemNamesAreUnique)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      for (const std::uint64_t seed : SEEDS)
      {
        const Frontier::Galaxy galaxy = GenerateOrFail(players, seed);

        std::set<std::string> seen;
        for (const Frontier::GalaxySystem& system : galaxy.Systems())
        {
          Assert::IsFalse(system.name.empty(), L"every system is named");
          Assert::IsTrue(seen.insert(system.name).second, (Where(players, seed) + L": a name came up twice").c_str());
        }
      }
    }
  }

  TEST_METHOD(NoLaneIsDuplicatedOrAttachedToItself)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      for (const std::uint64_t seed : SEEDS)
      {
        const Frontier::Galaxy galaxy = GenerateOrFail(players, seed);

        std::set<std::pair<std::int32_t, std::int32_t>> seen;
        for (const Frontier::GalaxyLane& lane : galaxy.Lanes())
        {
          Assert::IsTrue(lane.a < lane.b, L"endpoints ascend, so a lane has exactly one representation");
          Assert::IsTrue(seen.insert({lane.a.Index(), lane.b.Index()}).second, (Where(players, seed) + L": the same lane twice").c_str());
        }
      }
    }
  }

  // ADR-018, and the whole reason it exists: the galaxy is a pure function of (rules, seed).
  //
  // The hash is compared BETWEEN RUNS rather than against a literal. The sequence underneath is
  // what has to stay fixed forever and NeuronCoreTests pins that by value; the layout on top of it
  // is still Phase 0 tuning, and a literal here would fail on every deliberate change and teach
  // whoever is tuning to update it without reading it.
  TEST_METHOD(TheSameSeedGivesTheSameGalaxy)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      for (const std::uint64_t seed : SEEDS)
      {
        const Frontier::Galaxy first = GenerateOrFail(players, seed);
        const Frontier::Galaxy second = GenerateOrFail(players, seed);

        Assert::AreEqual(HashGalaxy(first), HashGalaxy(second), (Where(players, seed) + L": not reproducible").c_str());
      }
    }
  }

  TEST_METHOD(DifferentSeedsGiveDifferentGalaxies)
  {
    std::set<std::uint64_t> hashes;
    for (const std::uint64_t seed : SEEDS)
    {
      const Frontier::Galaxy galaxy = GenerateOrFail(8, seed);
      Assert::IsTrue(hashes.insert(HashGalaxy(galaxy)).second, L"two seeds produced the same galaxy");
    }
  }

  // Generation reuses the buffer it is handed, so it has to clear it first. Without this, a second
  // call would append a whole galaxy to the first and every count above would still pass.
  TEST_METHOD(GeneratingIntoAUsedGalaxyReplacesIt)
  {
    Frontier::Galaxy galaxy;
    (void)Frontier::GalaxyGenerator::TryGenerate(RulesFor(6), 1ULL, galaxy);
    const std::uint32_t systems = galaxy.SystemCount();
    const std::uint32_t lanes = galaxy.LaneCount();

    (void)Frontier::GalaxyGenerator::TryGenerate(RulesFor(6), 2ULL, galaxy);

    Assert::AreEqual(systems, galaxy.SystemCount(), L"the second galaxy replaced the first rather than joining it");
    Assert::AreEqual(lanes, galaxy.LaneCount());
  }

  // The layout satisfies the constraints by construction, so a run of refusals would mean the
  // generator and the rules have drifted apart. Zero here is the signal that they still agree.
  TEST_METHOD(GenerateAcceptsItsFirstSeed)
  {
    for (std::uint32_t players = Frontier::MINIMUM_PLAYERS; players <= Frontier::MAXIMUM_PLAYERS; ++players)
    {
      const Frontier::MatchRules rules = RulesFor(players);
      const Frontier::GeneratedGalaxy generated = Frontier::GalaxyGenerator::Generate(rules, 2026ULL);

      Assert::AreEqual(0U, generated.rejectedSeeds, L"the layout should satisfy its own constraints first time");
      Assert::IsTrue(Frontier::GalaxyGenerator::Validate(generated.galaxy, rules) == Frontier::GalaxyRejection::None);
      Assert::IsTrue(generated.seed != 0ULL, L"the accepted seed is reported, so a bug report can quote it");
    }
  }
};

// Rejection is the other half of the contract.
//
// The one-pager says a seed that cannot satisfy the constraints is rejected, and a validator that
// never says no is a validator nobody has tested. These hand-build small galaxies that break
// exactly one rule each, and check that the rule broken is the rule named: refused, not quietly
// repaired.
TEST_CLASS(GalaxyValidationTests)
{
public:
  /// Two capitals two ticks apart with the region hanging off the first. Small, and it passes.
  [[nodiscard]] static Frontier::Galaxy Acceptable()
  {
    Frontier::Galaxy galaxy;

    Frontier::GalaxySystem region;
    region.name = "Sealed Region";
    region.kind = Frontier::SystemKind::RegionAnchor;
    const Frontier::SystemId regionId = galaxy.AddSystem(std::move(region));

    Frontier::GalaxySystem first;
    first.name = "First";
    first.kind = Frontier::SystemKind::Capital;
    first.owner = Frontier::PlayerId{0};
    first.startingCluster = Frontier::PlayerId{0};
    const Frontier::SystemId firstId = galaxy.AddSystem(std::move(first));

    Frontier::GalaxySystem second;
    second.name = "Second";
    second.kind = Frontier::SystemKind::Capital;
    second.owner = Frontier::PlayerId{1};
    second.startingCluster = Frontier::PlayerId{1};
    const Frontier::SystemId secondId = galaxy.AddSystem(std::move(second));

    (void)galaxy.AddLane(firstId, secondId, 2);
    (void)galaxy.AddLane(firstId, regionId, 2);
    return galaxy;
  }

  TEST_METHOD(TheBaselineIsAccepted)
  {
    const Frontier::Galaxy galaxy = Acceptable();
    Assert::IsTrue(Frontier::GalaxyGenerator::Validate(galaxy, Frontier::MatchRules{}) == Frontier::GalaxyRejection::None,
                   L"the galaxy the other cases break must itself be sound, or they prove nothing");
  }

  TEST_METHOD(ASystemNothingReachesIsRefused)
  {
    Frontier::Galaxy galaxy = Acceptable();

    Frontier::GalaxySystem stranded;
    stranded.name = "Stranded";
    (void)galaxy.AddSystem(std::move(stranded));

    Assert::IsTrue(Frontier::GalaxyGenerator::Validate(galaxy, Frontier::MatchRules{}) == Frontier::GalaxyRejection::Disconnected);
  }

  TEST_METHOD(AClusterLaneThatDoesNotCostOneIsRefused)
  {
    Frontier::Galaxy galaxy = Acceptable();

    Frontier::GalaxySystem satellite;
    satellite.name = "Suburb";
    satellite.kind = Frontier::SystemKind::Satellite;
    satellite.startingCluster = Frontier::PlayerId{0};
    const Frontier::SystemId satelliteId = galaxy.AddSystem(std::move(satellite));

    // Inside the first cluster, so it must cost one. It costs two.
    (void)galaxy.AddLane(Frontier::SystemId{1}, satelliteId, 2);

    Assert::IsTrue(Frontier::GalaxyGenerator::Validate(galaxy, Frontier::MatchRules{}) == Frontier::GalaxyRejection::ClusterLaneNotOneTick);
  }

  TEST_METHOD(AFrontierLaneOutsideTheBandIsRefused)
  {
    Frontier::Galaxy galaxy;

    Frontier::GalaxySystem region;
    region.name = "Sealed Region";
    region.kind = Frontier::SystemKind::RegionAnchor;
    const Frontier::SystemId regionId = galaxy.AddSystem(std::move(region));

    Frontier::GalaxySystem first;
    first.name = "First";
    first.kind = Frontier::SystemKind::Capital;
    first.owner = Frontier::PlayerId{0};
    first.startingCluster = Frontier::PlayerId{0};
    const Frontier::SystemId firstId = galaxy.AddSystem(std::move(first));

    // Five ticks, outside the two-to-four band.
    (void)galaxy.AddLane(firstId, regionId, 5);

    Assert::IsTrue(Frontier::GalaxyGenerator::Validate(galaxy, Frontier::MatchRules{}) == Frontier::GalaxyRejection::FrontierLaneOutOfBand);
  }

  TEST_METHOD(AGalaxyWithoutARegionIsRefused)
  {
    Frontier::Galaxy galaxy;

    Frontier::GalaxySystem only;
    only.name = "Alone";
    only.kind = Frontier::SystemKind::Capital;
    only.owner = Frontier::PlayerId{0};
    (void)galaxy.AddSystem(std::move(only));

    Assert::IsTrue(Frontier::GalaxyGenerator::Validate(galaxy, Frontier::MatchRules{}) == Frontier::GalaxyRejection::RegionUnreachable);
  }

  TEST_METHOD(CapitalsTooFarApartAreRefused)
  {
    Frontier::Galaxy galaxy;

    Frontier::GalaxySystem region;
    region.name = "Sealed Region";
    region.kind = Frontier::SystemKind::RegionAnchor;
    const Frontier::SystemId regionId = galaxy.AddSystem(std::move(region));

    Frontier::GalaxySystem first;
    first.name = "First";
    first.kind = Frontier::SystemKind::Capital;
    first.owner = Frontier::PlayerId{0};
    first.startingCluster = Frontier::PlayerId{0};
    const Frontier::SystemId firstId = galaxy.AddSystem(std::move(first));

    Frontier::GalaxySystem second;
    second.name = "Second";
    second.kind = Frontier::SystemKind::Capital;
    second.owner = Frontier::PlayerId{1};
    second.startingCluster = Frontier::PlayerId{1};
    const Frontier::SystemId secondId = galaxy.AddSystem(std::move(second));

    // The only route between them runs through the region: two ticks each way, so four apart.
    (void)galaxy.AddLane(firstId, regionId, 2);
    (void)galaxy.AddLane(secondId, regionId, 2);

    Assert::IsTrue(Frontier::GalaxyGenerator::Validate(galaxy, Frontier::MatchRules{}) == Frontier::GalaxyRejection::RivalTooFar);
  }

  TEST_METHOD(APlayerCountOutsideTheDesignIsRefusedBeforeAnythingIsBuilt)
  {
    for (const std::uint32_t players : {0U, 1U, 5U, 13U, 100U})
    {
      Frontier::MatchRules rules;
      rules.playerCount = players;

      Frontier::Galaxy galaxy;
      const Frontier::GalaxyRejection rejection = Frontier::GalaxyGenerator::TryGenerate(rules, 1ULL, galaxy);

      Assert::IsTrue(rejection == Frontier::GalaxyRejection::PlayerCountOutOfRange, (L"player count " + std::to_wstring(players)).c_str());
      Assert::AreEqual(0U, galaxy.SystemCount(), L"refused before building, not built and then thrown away");
    }
  }

  // A rejection nobody can read is a rejection nobody can act on.
  TEST_METHOD(EveryRejectionDescribesItself)
  {
    constexpr std::array<Frontier::GalaxyRejection, 7> ALL = {
      Frontier::GalaxyRejection::None,
      Frontier::GalaxyRejection::PlayerCountOutOfRange,
      Frontier::GalaxyRejection::Disconnected,
      Frontier::GalaxyRejection::RivalTooFar,
      Frontier::GalaxyRejection::ClusterLaneNotOneTick,
      Frontier::GalaxyRejection::FrontierLaneOutOfBand,
      Frontier::GalaxyRejection::RegionUnreachable,
    };

    std::set<std::string> descriptions;
    for (const Frontier::GalaxyRejection rejection : ALL)
    {
      const char* text = Frontier::Describe(rejection);
      Assert::IsNotNull(text);
      Assert::IsTrue(descriptions.insert(text).second, L"two rejections share a description");
    }
  }
};

// The graph itself, apart from anything that generates one.
TEST_CLASS(GalaxyGraphTests)
{
public:
  TEST_METHOD(ShortestPathCountsTicksRatherThanLanes)
  {
    Frontier::Galaxy galaxy;
    const Frontier::SystemId a = galaxy.AddSystem(Frontier::GalaxySystem{.name = "A"});
    const Frontier::SystemId b = galaxy.AddSystem(Frontier::GalaxySystem{.name = "B"});
    const Frontier::SystemId c = galaxy.AddSystem(Frontier::GalaxySystem{.name = "C"});

    // One long hop, or two short ones. The pair is further in lanes and nearer in time, and time is
    // what the one-pager measures with.
    (void)galaxy.AddLane(a, c, 9);
    (void)galaxy.AddLane(a, b, 2);
    (void)galaxy.AddLane(b, c, 3);

    Assert::AreEqual(5U, galaxy.ShortestPathTicks(a, c), L"two lanes at 2 and 3 beat one at 9");
    Assert::AreEqual(0U, galaxy.ShortestPathTicks(a, a));
  }

  TEST_METHOD(AnUnreachableSystemSaysSo)
  {
    Frontier::Galaxy galaxy;
    const Frontier::SystemId a = galaxy.AddSystem(Frontier::GalaxySystem{.name = "A"});
    const Frontier::SystemId b = galaxy.AddSystem(Frontier::GalaxySystem{.name = "B"});

    Assert::AreEqual(Frontier::Galaxy::UNREACHABLE, galaxy.ShortestPathTicks(a, b));
    Assert::IsFalse(galaxy.IsConnected());
  }

  TEST_METHOD(SelfLoopsAndDuplicatesAreRefused)
  {
    Frontier::Galaxy galaxy;
    const Frontier::SystemId a = galaxy.AddSystem(Frontier::GalaxySystem{.name = "A"});
    const Frontier::SystemId b = galaxy.AddSystem(Frontier::GalaxySystem{.name = "B"});

    Assert::IsFalse(galaxy.AddLane(a, a, 1).IsValid(), L"a system is not next to itself");
    Assert::IsTrue(galaxy.AddLane(a, b, 1).IsValid());
    Assert::IsFalse(galaxy.AddLane(a, b, 1).IsValid(), L"the same lane twice");
    Assert::IsFalse(galaxy.AddLane(b, a, 1).IsValid(), L"nor the same lane backwards");
    Assert::AreEqual(1U, galaxy.LaneCount());
  }

  TEST_METHOD(BothEndsOfALaneKnowAboutIt)
  {
    Frontier::Galaxy galaxy;
    const Frontier::SystemId a = galaxy.AddSystem(Frontier::GalaxySystem{.name = "A"});
    const Frontier::SystemId b = galaxy.AddSystem(Frontier::GalaxySystem{.name = "B"});
    const Frontier::LaneId lane = galaxy.AddLane(b, a, 4);

    Assert::AreEqual(static_cast<size_t>(1), galaxy.LanesAt(a).size());
    Assert::AreEqual(static_cast<size_t>(1), galaxy.LanesAt(b).size());
    Assert::IsTrue(galaxy.OtherEnd(lane, a) == b);
    Assert::IsTrue(galaxy.OtherEnd(lane, b) == a);
    Assert::AreEqual(4U, galaxy.LaneAt(lane).costTicks, L"the cost survives the endpoint normalisation");
  }
};

} // namespace GameLogicTests
