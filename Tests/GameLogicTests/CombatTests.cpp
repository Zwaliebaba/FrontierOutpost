// CombatTests.cpp -- phase 4, both sub-phases.
//
// Step 5 of Design/Plans/4X-01-CoreLoop.md. The arithmetic and the reasoning are ADR-021; these
// check the properties the one-pager states, which are the part that must survive Phase 0 changing
// every number.
//
// AN ARRIVAL IS SET UP AS A FLEET IN TRANSIT WITH ONE TICK LEFT, never by setting
// `arrivedThisTick` directly. The movement phase clears that flag for every fleet before it sets
// it, which is correct -- a fleet that arrived three ticks ago is an incumbent -- and it means a
// test that wrote the flag would have it wiped before combat ever saw it. Arriving through
// movement is also what actually happens in a match.

#include "pch.h"
#include "CppUnitTest.h"

#include "TickResolver.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint64_t SEED = 0x4652'4F4E'5449'4552ULL;

[[nodiscard]] Frontier::Match Arena(Frontier::MatchRules _rules = {})
{
  _rules.playerCount = 6;
  Frontier::Match match = Frontier::Match::Create(_rules, SEED);

  // The starting fleets are in the way of every scenario below, so they are moved off the board.
  for (Frontier::MatchFleet& fleet : match.MutableFleets())
  {
    fleet.destroyed = true;
    fleet.at = Frontier::SystemId{};
  }

  // Past the capital guard, so a fight at a capital is a fight rather than a rule.
  match.SetTick(match.Rules().capitalGuardTicks);
  return match;
}

/// A fleet already standing at a system. An incumbent, in the one-pager's sense.
Frontier::FleetId Holding(Frontier::Match& _match, std::int32_t _player, std::uint32_t _ships, Frontier::SystemId _at)
{
  Frontier::MatchFleet fleet;
  fleet.owner = Frontier::PlayerId{_player};
  fleet.ships = _ships;
  fleet.at = _at;
  return _match.AddFleet(fleet);
}

/// A fleet that will land at `_to` during this tick's movement phase.
Frontier::FleetId Arriving(Frontier::Match& _match, std::int32_t _player, std::uint32_t _ships, Frontier::SystemId _from,
                           Frontier::SystemId _to)
{
  Frontier::MatchFleet fleet;
  fleet.owner = Frontier::PlayerId{_player};
  fleet.ships = _ships;
  fleet.movingFrom = _from;
  fleet.movingTo = _to;
  fleet.ticksRemaining = 1;
  return _match.AddFleet(fleet);
}

[[nodiscard]] Frontier::SystemId NeighborOf(const Frontier::Match& _match, Frontier::SystemId _from)
{
  const Frontier::LaneId lane = _match.GalaxyGraph().LanesAt(_from).front();
  return _match.GalaxyGraph().OtherEnd(lane, _from);
}

[[nodiscard]] Frontier::Match Fight(const Frontier::Match& _match)
{
  Frontier::TickLog log;
  return Frontier::TickResolver::Resolve(_match, {}, log);
}

[[nodiscard]] bool HasDigestKind(const Frontier::TickLog& _log, std::int32_t _player, Frontier::DigestKind _kind)
{
  const std::vector<Frontier::DigestEntry>& digest = _log.digests[static_cast<std::size_t>(_player)];
  return std::any_of(digest.begin(), digest.end(), [_kind](const Frontier::DigestEntry& _entry) { return _entry.kind == _kind; });
}

} // namespace

TEST_CLASS(CombatTests)
{
public:
  // The design reference's own preview, used as ONE CASE AND NOT AS A TARGET.
  //
  // `Design/Screens/README.md` shows "preview: 14 v 11 (+def) - 6 left", and the plan is explicit:
  // do not tune to it. The parameters in `MatchRules` were chosen for shape -- half strength a
  // round, a quarter bonus, three rounds -- and this is what they happen to produce. It comes out
  // at exactly six, which is a pleasing check on the arithmetic and nothing more. **If Phase 0
  // moves a parameter, change the number here rather than the parameter.**
  TEST_METHOD(TheReferencePreviewComesOutAtSix)
  {
    Frontier::Match match = Arena();
    const Frontier::SystemId contested = match.GalaxyGraph().Capitals()[1];
    const Frontier::SystemId approach = NeighborOf(match, contested);

    const Frontier::FleetId defender = Holding(match, 1, 11, contested);
    const Frontier::FleetId attacker = Arriving(match, 0, 14, approach, contested);

    const Frontier::Match after = Fight(match);

    Assert::AreEqual(6U, after.FleetAt(attacker).ships, L"fourteen against eleven with the defender bonus");
    Assert::AreEqual(0U, after.FleetAt(defender).ships);
    Assert::IsTrue(after.FleetAt(defender).destroyed);
  }

  // "Each fleet's damage spread across enemies in proportion to strength." The bigger enemy soaks
  // more of it, which is what stops a large fleet hiding behind a small one.
  TEST_METHOD(DamageSpreadsInProportionToEnemyStrength)
  {
    Frontier::Match match = Arena();
    const Frontier::SystemId where = match.GalaxyGraph().Capitals()[3];

    const Frontier::FleetId big = Holding(match, 1, 10, where);
    const Frontier::FleetId small = Holding(match, 2, 5, where);
    (void)Holding(match, 0, 20, where);

    const Frontier::Match after = Fight(match);

    const std::uint32_t bigLost = 10 - after.FleetAt(big).ships;
    const std::uint32_t smallLost = 5 - after.FleetAt(small).ships;
    Assert::IsTrue(bigLost > smallLost, L"the larger enemy takes the larger share");
  }

  TEST_METHOD(DamageSpreadsAcrossThreeEnemiesToo)
  {
    Frontier::Match match = Arena();
    const Frontier::SystemId where = match.GalaxyGraph().Capitals()[3];

    const Frontier::FleetId first = Holding(match, 1, 12, where);
    const Frontier::FleetId second = Holding(match, 2, 8, where);
    const Frontier::FleetId third = Holding(match, 3, 4, where);
    (void)Holding(match, 0, 30, where);

    const Frontier::Match after = Fight(match);

    const std::uint32_t firstLost = 12 - after.FleetAt(first).ships;
    const std::uint32_t secondLost = 8 - after.FleetAt(second).ships;
    const std::uint32_t thirdLost = 4 - after.FleetAt(third).ships;

    Assert::IsTrue(firstLost >= secondLost, L"twelve absorbs at least what eight does");
    Assert::IsTrue(secondLost >= thirdLost, L"and eight at least what four does");
    Assert::IsTrue(firstLost > 0 && thirdLost > 0, L"everybody is in the fight");
  }

  // "An incumbent fleet gets the defender bonus." Equal fleets, and the one that was already there
  // comes out ahead -- which is the whole reason the defender's bet is whether to stay.
  TEST_METHOD(TheIncumbentGetsTheDefenderBonus)
  {
    Frontier::Match match = Arena();
    const Frontier::SystemId where = match.GalaxyGraph().Capitals()[2];
    const Frontier::SystemId approach = NeighborOf(match, where);

    const Frontier::FleetId incumbent = Holding(match, 2, 10, where);
    const Frontier::FleetId arriving = Arriving(match, 0, 10, approach, where);

    const Frontier::Match after = Fight(match);

    Assert::IsTrue(after.FleetAt(incumbent).ships > after.FleetAt(arriving).ships,
                   L"equal fleets, and the one that was already there wins");
  }

  // "Simultaneous arrivals at an empty system get none." Nobody was there, so nobody is defending
  // anything -- and this is why incumbency is a property of the FLEET, not of who owns the system.
  TEST_METHOD(SimultaneousArrivalsAtAnEmptySystemGetNoBonus)
  {
    Frontier::Match match = Arena();

    // A frontier system, unowned and unoccupied.
    Frontier::SystemId empty;
    for (std::size_t index = 0; index < match.Systems().size(); ++index)
    {
      const Frontier::SystemId candidate{static_cast<std::int32_t>(index)};
      if (!match.SystemAt(candidate).owner.IsValid() && match.GalaxyGraph().SystemAt(candidate).kind == Frontier::SystemKind::Frontier)
      {
        empty = candidate;
        break;
      }
    }
    Assert::IsTrue(empty.IsValid());

    const std::vector<Frontier::LaneId>& lanes = match.GalaxyGraph().LanesAt(empty);
    Assert::IsTrue(lanes.size() >= 2, L"this test needs two ways in");

    const Frontier::FleetId first = Arriving(match, 0, 10, match.GalaxyGraph().OtherEnd(lanes[0], empty), empty);
    const Frontier::FleetId second = Arriving(match, 1, 10, match.GalaxyGraph().OtherEnd(lanes[1], empty), empty);

    const Frontier::Match after = Fight(match);

    Assert::AreEqual(after.FleetAt(first).ships, after.FleetAt(second).ships, L"neither of them was there first");
    Assert::IsFalse(after.SystemAt(empty).owner.IsValid(), L"and two survivors leave it occupied and unclaimed");
  }

  // "A tie is mutual attrition, not a coin flip."
  TEST_METHOD(ATieLeavesBothAliveAndBothSmaller)
  {
    Frontier::Match match = Arena();
    const Frontier::SystemId where = match.GalaxyGraph().Capitals()[4];

    const Frontier::FleetId left = Holding(match, 0, 10, where);
    const Frontier::FleetId right = Holding(match, 1, 10, where);

    const Frontier::Match after = Fight(match);

    Assert::AreEqual(after.FleetAt(left).ships, after.FleetAt(right).ships, L"the same both sides");
    Assert::IsTrue(after.FleetAt(left).ships > 0, L"and both still standing");
    Assert::IsTrue(after.FleetAt(left).ships < 10, L"and both smaller than they were");
    Assert::IsFalse(after.SystemAt(where).owner == Frontier::PlayerId{0}, L"nobody took it");
  }

  // "Fleets pass each other on lanes; combat happens only at systems."
  TEST_METHOD(FleetsPassEachOtherOnALaneWithoutFighting)
  {
    Frontier::Match match = Arena();
    const Frontier::SystemId capital = match.GalaxyGraph().Capitals()[0];

    // A lane that takes more than one tick, so both are still on it when the tick resolves.
    Frontier::LaneId slow;
    for (const Frontier::LaneId lane : match.GalaxyGraph().LanesAt(capital))
    {
      if (match.GalaxyGraph().LaneAt(lane).costTicks > 1)
      {
        slow = lane;
        break;
      }
    }
    Assert::IsTrue(slow.IsValid());
    const Frontier::SystemId far = match.GalaxyGraph().OtherEnd(slow, capital);

    Frontier::MatchFleet outbound;
    outbound.owner = Frontier::PlayerId{0};
    outbound.ships = 10;
    outbound.movingFrom = capital;
    outbound.movingTo = far;
    outbound.ticksRemaining = 2;
    const Frontier::FleetId first = match.AddFleet(outbound);

    Frontier::MatchFleet inbound;
    inbound.owner = Frontier::PlayerId{1};
    inbound.ships = 10;
    inbound.movingFrom = far;
    inbound.movingTo = capital;
    inbound.ticksRemaining = 2;
    const Frontier::FleetId second = match.AddFleet(inbound);

    const Frontier::Match after = Fight(match);

    Assert::AreEqual(10U, after.FleetAt(first).ships, L"they passed");
    Assert::AreEqual(10U, after.FleetAt(second).ships);
  }

  // The one-pager's central consequence, now with combat live rather than stubbed. The defender
  // that leaves would certainly have died had it stayed; movement is phase 3 and combat is phase 4.
  TEST_METHOD(LeavingStillBeatsArrivingNowThatCombatIsReal)
  {
    Frontier::Match match = Arena();
    const Frontier::SystemId home = match.GalaxyGraph().Capitals()[0];
    const Frontier::SystemId away = NeighborOf(match, home);

    const Frontier::FleetId defender = Holding(match, 0, 5, home);
    const Frontier::FleetId raider = Arriving(match, 1, 50, away, home);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = defender, .destination = away});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(match, sets, log);

    Assert::AreEqual(5U, after.FleetAt(defender).ships, L"it got out with everything");
    Assert::IsFalse(after.FleetAt(defender).destroyed);
    Assert::AreEqual(50U, after.FleetAt(raider).ships, L"and the raider found an empty system");
  }

  TEST_METHOD(ADestroyedFleetIsMarkedRatherThanErased)
  {
    Frontier::Match match = Arena();
    const Frontier::SystemId where = match.GalaxyGraph().Capitals()[5];

    const Frontier::FleetId doomed = Holding(match, 1, 2, where);
    (void)Holding(match, 0, 60, where);

    const std::size_t fleetCount = match.Fleets().size();
    const Frontier::Match after = Fight(match);

    Assert::AreEqual(fleetCount, after.Fleets().size(), L"ids in an old digest still have to resolve");
    Assert::IsTrue(after.FleetAt(doomed).destroyed);
    Assert::AreEqual(0U, after.FleetAt(doomed).ships);
    Assert::IsFalse(after.FleetAt(doomed).at.IsValid(), L"and it is nowhere");
    Assert::IsTrue(after.IsConsistent());
  }

  TEST_METHOD(ABattleReachesBothSidesDigests)
  {
    Frontier::Match match = Arena();
    const Frontier::SystemId where = match.GalaxyGraph().Capitals()[3];
    (void)Holding(match, 0, 10, where);
    (void)Holding(match, 3, 10, where);

    Frontier::TickLog log;
    (void)Frontier::TickResolver::Resolve(match, {}, log);

    Assert::IsTrue(HasDigestKind(log, 0, Frontier::DigestKind::Battle));
    Assert::IsTrue(HasDigestKind(log, 3, Frontier::DigestKind::Battle));
    Assert::IsFalse(HasDigestKind(log, 4, Frontier::DigestKind::Battle), L"and nobody else's");
  }

  // ADR-018 at the level the one-pager cares about most: "uncertainty comes from what humans
  // ordered, not from dice".
  TEST_METHOD(TheSameBattleResolvesTheSameWayTwice)
  {
    const auto build = []
    {
      Frontier::Match match = Arena();
      const Frontier::SystemId where = match.GalaxyGraph().Capitals()[2];
      (void)Holding(match, 0, 17, where);
      (void)Holding(match, 1, 13, where);
      (void)Holding(match, 2, 9, where);
      return match;
    };

    Assert::AreEqual(Fight(build()).Hash(), Fight(build()).Hash());
  }

  // A fight with no defender bonus anywhere still has to be symmetric under swapping the sides:
  // if it were not, the order fleets are considered in would be deciding it.
  TEST_METHOD(WhichSideIsListedFirstDoesNotDecideTheBattle)
  {
    const auto run = [](std::int32_t _first, std::int32_t _second)
    {
      Frontier::Match match = Arena();
      const Frontier::SystemId where = match.GalaxyGraph().Capitals()[4];
      const Frontier::SystemId approach = NeighborOf(match, where);
      const Frontier::FleetId one = Arriving(match, _first, 12, approach, where);
      const Frontier::FleetId two = Arriving(match, _second, 12, approach, where);
      const Frontier::Match after = Fight(match);
      return std::pair{after.FleetAt(one).ships, after.FleetAt(two).ships};
    };

    const auto [firstOfAB, secondOfAB] = run(0, 1);
    const auto [firstOfBA, secondOfBA] = run(1, 0);

    Assert::AreEqual(firstOfAB, secondOfAB, L"equal fleets lose equally");
    Assert::AreEqual(firstOfAB, secondOfBA, L"and swapping the players changes nothing");
    Assert::AreEqual(secondOfAB, firstOfBA);
  }
};

// Sub-phase 4a. Built, switched off, and tested in both positions -- a switch that has never been
// on is a switch that does not work.
TEST_CLASS(RearGuardTests)
{
public:
  /// A fleet leaving a system a hostile is arriving at, in the same tick.
  struct Withdrawal
  {
    Frontier::Match match;
    Frontier::FleetId leaving;
    Frontier::FleetId arriving;
    Frontier::SystemId from;
    Frontier::SystemId to;
  };

  [[nodiscard]] static Withdrawal Setup(bool _rearGuardEnabled)
  {
    Frontier::MatchRules rules;
    rules.rearGuardEnabled = _rearGuardEnabled;

    Frontier::Match match = Arena(rules);
    const Frontier::SystemId home = match.GalaxyGraph().Capitals()[0];
    const Frontier::SystemId away = NeighborOf(match, home);

    const Frontier::FleetId leaving = Holding(match, 0, 20, home);
    const Frontier::FleetId arriving = Arriving(match, 1, 10, away, home);
    return Withdrawal{.match = match, .leaving = leaving, .arriving = arriving, .from = home, .to = away};
  }

  [[nodiscard]] static Frontier::Match Withdraw(const Withdrawal& _setup, Frontier::TickLog& _log)
  {
    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = _setup.leaving, .destination = _setup.to});
    const std::array<Frontier::OrderSet, 1> sets = {orders};
    return Frontier::TickResolver::Resolve(_setup.match, sets, _log);
  }

  TEST_METHOD(TheRearGuardIsOffByDefault)
  {
    Assert::IsFalse(Frontier::MatchRules{}.rearGuardEnabled, L"the one-pager keeps it off until Phase 0 shows dancing dominates");

    const Withdrawal setup = Setup(false);
    Frontier::TickLog log;
    const Frontier::Match after = Withdraw(setup, log);

    Assert::AreEqual(20U, after.FleetAt(setup.leaving).ships, L"dancing is free while it is off");
  }

  // "Fleets that departed a system this tick while a hostile arrived there take one free round from
  // the arrivals, computed from the arrivals' end-of-movement strength."
  TEST_METHOD(WhenOnTheDepartingFleetTakesExactlyOneRound)
  {
    const Withdrawal setup = Setup(true);
    Frontier::TickLog log;
    const Frontier::Match after = Withdraw(setup, log);

    // One round at the ordinary rate from ten arriving ships: five.
    const std::uint32_t expected = 20 - (10 * Frontier::MatchRules{}.damagePercentPerRound) / 100;
    Assert::AreEqual(expected, after.FleetAt(setup.leaving).ships);
    Assert::IsTrue(after.FleetAt(setup.leaving).at == setup.to, L"and it still got away");
    Assert::AreEqual(10U, after.FleetAt(setup.arriving).ships, L"the arrivals take nothing back");
  }

  TEST_METHOD(TheRearGuardOnlyFiresWhenAHostileActuallyArrives)
  {
    Frontier::MatchRules rules;
    rules.rearGuardEnabled = true;

    Frontier::Match match = Arena(rules);
    const Frontier::SystemId home = match.GalaxyGraph().Capitals()[0];
    const Frontier::SystemId away = NeighborOf(match, home);
    const Frontier::FleetId leaving = Holding(match, 0, 20, home);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = leaving, .destination = away});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(match, sets, log);

    Assert::AreEqual(20U, after.FleetAt(leaving).ships, L"an ordinary move is not a withdrawal under fire");
  }

  TEST_METHOD(YourOwnFleetArrivingIsNotAHostile)
  {
    Frontier::MatchRules rules;
    rules.rearGuardEnabled = true;

    Frontier::Match match = Arena(rules);
    const Frontier::SystemId home = match.GalaxyGraph().Capitals()[0];
    const Frontier::SystemId away = NeighborOf(match, home);

    const Frontier::FleetId leaving = Holding(match, 0, 20, home);
    (void)Arriving(match, 0, 10, away, home);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = leaving, .destination = away});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(match, sets, log);

    Assert::AreEqual(20U, after.FleetAt(leaving).ships, L"a relief column does not shoot the garrison");
  }

  // 4a feeds 4b: a fleet weakened on its way out is weaker wherever it lands. The one-pager says
  // so in as many words -- "production feeds movement, 4a feeds 4b".
  TEST_METHOD(TheRearGuardsLossesCarryIntoTheBattleAtTheDestination)
  {
    Frontier::MatchRules rules;
    rules.rearGuardEnabled = true;

    Frontier::Match match = Arena(rules);
    const Frontier::SystemId home = match.GalaxyGraph().Capitals()[0];
    const Frontier::SystemId away = NeighborOf(match, home);

    const Frontier::FleetId leaving = Holding(match, 0, 20, home);
    (void)Arriving(match, 1, 10, away, home);
    const Frontier::FleetId waiting = Holding(match, 2, 12, away);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = leaving, .destination = away});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(match, sets, log);

    // It left with twenty, five were taken on the way out, and it arrived into a fight with an
    // incumbent twelve. What is being asserted is the ordering, not the arithmetic: it must be
    // worse off than the same withdrawal into an empty system.
    const Withdrawal quiet = Setup(true);
    Frontier::TickLog quietLog;
    const Frontier::Match quietAfter = Withdraw(quiet, quietLog);

    Assert::IsTrue(after.FleetAt(leaving).ships < quietAfter.FleetAt(quiet.leaving).ships,
                   L"the rear-guard's losses were still gone when the second fight started");
    Assert::IsTrue(after.FleetAt(waiting).ships < 12U, L"and the fight at the destination happened");
  }
};

} // namespace GameLogicTests
