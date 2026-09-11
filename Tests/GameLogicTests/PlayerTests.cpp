// PlayerTests.cpp -- presence, custodian, guard, score, and the ending.
//
// Step 7 of Design/Plans/4X-01-CoreLoop.md. The one-pager allows four player states and six
// transitions and says "nothing else exists", so the tests below walk the transitions that are
// reachable in Stage A and assert that the two that are not stay unreachable.
//
// PRESENCE IS TOLD TO THE SIMULATION, NEVER INFERRED. `TickInput::present` is what the server saw;
// `presenceUnknown` is what a caller with nothing to say about it means. A test about absence has
// to set both, and the helper below is the only place that does.

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

[[nodiscard]] Frontier::Match SixPlayers(Frontier::MatchRules _rules = {})
{
  _rules.playerCount = 6;
  return Frontier::Match::Create(_rules, SEED);
}

/// Resolves a tick in which `_absent` was not seen and everybody else was.
[[nodiscard]] Frontier::Match WithoutPlayer(const Frontier::Match& _match, std::int32_t _absent, Frontier::TickLog& _log)
{
  std::vector<Frontier::PlayerId> present;
  for (std::size_t index = 0; index < _match.Players().size(); ++index)
  {
    if (static_cast<std::int32_t>(index) != _absent)
    {
      present.emplace_back(static_cast<std::int32_t>(index));
    }
  }

  return Frontier::TickResolver::Resolve(_match, Frontier::TickInput{.present = present, .presenceUnknown = false}, _log);
}

[[nodiscard]] Frontier::Match WithoutPlayer(const Frontier::Match& _match, std::int32_t _absent)
{
  Frontier::TickLog log;
  return WithoutPlayer(_match, _absent, log);
}

[[nodiscard]] Frontier::Match Everyone(const Frontier::Match& _match, Frontier::TickLog& _log)
{
  return Frontier::TickResolver::Resolve(_match, {}, _log);
}

[[nodiscard]] Frontier::Match Everyone(const Frontier::Match& _match)
{
  Frontier::TickLog log;
  return Everyone(_match, log);
}

[[nodiscard]] bool HasDigestKind(const Frontier::TickLog& _log, std::int32_t _player, Frontier::DigestKind _kind)
{
  const std::vector<Frontier::DigestEntry>& digest = _log.digests[static_cast<std::size_t>(_player)];
  return std::any_of(digest.begin(), digest.end(), [_kind](const Frontier::DigestEntry& _entry) { return _entry.kind == _kind; });
}

[[nodiscard]] Frontier::SystemId NeighborOf(const Frontier::Match& _match, Frontier::SystemId _from)
{
  const Frontier::LaneId lane = _match.GalaxyGraph().LanesAt(_from).front();
  return _match.GalaxyGraph().OtherEnd(lane, _from);
}

} // namespace

TEST_CLASS(PresenceAndCustodianTests)
{
public:
  TEST_METHOD(EverybodyStartsActive)
  {
    const Frontier::Match match = SixPlayers();
    for (const Frontier::PlayerState& player : match.Players())
    {
      Assert::IsTrue(player.status == Frontier::PlayerStatus::Active);
      Assert::AreEqual(0U, player.absentTicks);
      Assert::IsFalse(player.conceded);
    }
  }

  // "Entered by three ticks of absence." Two is not three.
  TEST_METHOD(ThreeAbsentTicksMakeACustodian)
  {
    Frontier::Match match = SixPlayers();
    const std::uint32_t needed = match.Rules().custodianAbsenceTicks;

    for (std::uint32_t tick = 1; tick < needed; ++tick)
    {
      match = WithoutPlayer(match, 0);
      Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Active,
                     (std::wstring(L"still active after ") + std::to_wstring(tick) + L" absent ticks").c_str());
    }

    Frontier::TickLog log;
    match = WithoutPlayer(match, 0, log);

    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Custodian);
    Assert::AreEqual(needed, match.PlayerAt(Frontier::PlayerId{0}).absentTicks);
    Assert::IsTrue(HasDigestKind(log, 0, Frontier::DigestKind::Custodian));
  }

  // "Custodians are flagged on every player's map as 'custodian since tick N'." Every player's.
  TEST_METHOD(EverybodyIsToldWhenSomebodyGoesIntoCustody)
  {
    Frontier::Match match = SixPlayers();
    for (std::uint32_t tick = 1; tick < match.Rules().custodianAbsenceTicks; ++tick)
    {
      match = WithoutPlayer(match, 2);
    }

    Frontier::TickLog log;
    match = WithoutPlayer(match, 2, log);

    for (std::int32_t player = 0; player < 6; ++player)
    {
      Assert::IsTrue(HasDigestKind(log, player, Frontier::DigestKind::Custodian),
                     (std::wstring(L"player ") + std::to_wstring(player) + L" was not told").c_str());
    }
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{2}).custodianSince > 0);
  }

  // "Reversible -- log in and resume."
  TEST_METHOD(ComingBackEndsCustodyImmediately)
  {
    Frontier::Match match = SixPlayers();
    for (std::uint32_t tick = 0; tick < match.Rules().custodianAbsenceTicks; ++tick)
    {
      match = WithoutPlayer(match, 0);
    }
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Custodian);

    Frontier::TickLog log;
    match = Everyone(match, log);

    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Active);
    Assert::AreEqual(0U, match.PlayerAt(Frontier::PlayerId{0}).absentTicks);
    Assert::AreEqual(0U, match.PlayerAt(Frontier::PlayerId{0}).custodianSince);
    Assert::IsTrue(HasDigestKind(log, 0, Frontier::DigestKind::Custodian), L"the return is news too");
  }

  TEST_METHOD(AnInterruptedAbsenceStartsCountingAgain)
  {
    Frontier::Match match = SixPlayers();
    match = WithoutPlayer(match, 0);
    match = WithoutPlayer(match, 0);
    Assert::AreEqual(2U, match.PlayerAt(Frontier::PlayerId{0}).absentTicks);

    match = Everyone(match);
    Assert::AreEqual(0U, match.PlayerAt(Frontier::PlayerId{0}).absentTicks, L"one appearance clears it");

    match = WithoutPlayer(match, 0);
    Assert::AreEqual(1U, match.PlayerAt(Frontier::PlayerId{0}).absentTicks);
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Active);
  }

  // Presence is a fact about logging in, not about submitting. A player who looks at the board and
  // changes nothing is present.
  TEST_METHOD(BeingPresentWithoutOrdersIsStillBeingPresent)
  {
    Frontier::Match match = SixPlayers();

    const std::array<Frontier::PlayerId, 6> everybody = {
      Frontier::PlayerId{0}, Frontier::PlayerId{1}, Frontier::PlayerId{2},
      Frontier::PlayerId{3}, Frontier::PlayerId{4}, Frontier::PlayerId{5},
    };

    for (std::uint32_t tick = 0; tick < match.Rules().custodianAbsenceTicks + 2; ++tick)
    {
      Frontier::TickLog log;
      match = Frontier::TickResolver::Resolve(match, Frontier::TickInput{.present = everybody, .presenceUnknown = false}, log);
    }

    for (const Frontier::PlayerState& player : match.Players())
    {
      Assert::IsTrue(player.status == Frontier::PlayerStatus::Active, L"nobody submitted anything and nobody is absent");
    }
  }

  // "Territory defends, never expands, never attacks." The orders are discarded at the lock.
  TEST_METHOD(ACustodiansOrdersAreDiscarded)
  {
    Frontier::Match match = SixPlayers();
    for (std::uint32_t tick = 0; tick < match.Rules().custodianAbsenceTicks; ++tick)
    {
      match = WithoutPlayer(match, 0);
    }

    const Frontier::SystemId capital = match.GalaxyGraph().Capitals()[0];
    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.builds.push_back(Frontier::BuildOrder{.system = capital, .kind = Frontier::BuildKind::MiningStation});

    const std::vector<Frontier::RejectedOrder> rejected = match.Validate(orders);
    Assert::AreEqual(static_cast<size_t>(1), rejected.size(), L"one refusal for the set, not one per order");
    Assert::IsTrue(rejected.front().reason == Frontier::OrderRejection::YouAreACustodian);

    // Resolved with the player still absent. Submitting orders is not what ends custody -- being
    // seen is -- so a tick that reported them present would end it before validation ran and the
    // build would go through. Which is right, and is not what this test is about.
    const std::array<Frontier::OrderSet, 1> sets = {orders};
    const std::array<Frontier::PlayerId, 5> others = {Frontier::PlayerId{1}, Frontier::PlayerId{2}, Frontier::PlayerId{3},
                                                      Frontier::PlayerId{4}, Frontier::PlayerId{5}};

    Frontier::TickLog log;
    const Frontier::Match after =
      Frontier::TickResolver::Resolve(match, Frontier::TickInput{.orders = sets, .present = others, .presenceUnknown = false}, log);

    Assert::IsFalse(after.SystemAt(capital).hasMiningStation, L"and nothing was built");
  }

  // A custodian's fleets hold what they stand on and take nothing.
  TEST_METHOD(ACustodianClaimsNothingEvenStandingOnIt)
  {
    Frontier::Match match = SixPlayers();
    const Frontier::SystemId open = NeighborOf(match, match.GalaxyGraph().Capitals()[0]);
    Assert::IsFalse(match.SystemAt(open).owner.IsValid());

    // Lapse FIRST, then park the fleet. Parking it first would have it claim the system during the
    // two active ticks before custody began, which is correct and is not what this test is about.
    for (std::uint32_t tick = 0; tick < match.Rules().custodianAbsenceTicks; ++tick)
    {
      match = WithoutPlayer(match, 0);
    }
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Custodian);
    Assert::IsFalse(match.SystemAt(open).owner.IsValid());

    match.MutableFleets()[0].at = open;
    match.MutableFleets()[0].ships = 10;
    match = WithoutPlayer(match, 0);
    Assert::IsFalse(match.SystemAt(open).owner.IsValid(), L"custodian territory never expands");
  }

  // "Their garrisons weaken with each tick of absence, so the territory is a public race."
  TEST_METHOD(ACustodiansGarrisonsWeakenEveryTick)
  {
    Frontier::Match match = SixPlayers();
    for (std::uint32_t tick = 0; tick < match.Rules().custodianAbsenceTicks; ++tick)
    {
      match = WithoutPlayer(match, 0);
    }

    const std::uint32_t before = match.FleetAt(Frontier::FleetId{0}).ships;
    match = WithoutPlayer(match, 0);
    const std::uint32_t after = match.FleetAt(Frontier::FleetId{0}).ships;

    Assert::IsTrue(after < before, L"a garrison nobody is commanding decays");

    // An active player's does not.
    Assert::AreEqual(match.Rules().startingShips, match.FleetAt(Frontier::FleetId{1}).ships);
  }

  // "Conceding never denies an attacker their prize" -- permanent, and the territory stays takeable.
  TEST_METHOD(ConcedingIsPermanentCustody)
  {
    Frontier::Match match = SixPlayers();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.concede = true;
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    Frontier::TickLog log;
    match = Frontier::TickResolver::Resolve(match, {.orders = sets}, log);

    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).conceded);
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Custodian);
    Assert::IsTrue(HasDigestKind(log, 3, Frontier::DigestKind::Custodian), L"everybody is told");
    Assert::IsTrue(match.IsConsistent());

    // And unlike absence, showing up does not undo it.
    for (std::int32_t tick = 0; tick < 3; ++tick)
    {
      match = Everyone(match);
    }
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Custodian, L"concession is not reversible");

    // The capital is still on the board for somebody to take.
    Assert::IsTrue(match.SystemAt(match.GalaxyGraph().Capitals()[0]).owner == Frontier::PlayerId{0});
  }

  // "Systems conquered from a custodian yield at half for the rest of the match, whoever holds
  // them." Whoever -- so a second capture does not launder it.
  TEST_METHOD(HalfYieldStampsTheSystemAndSurvivesASecondCapture)
  {
    Frontier::Match match = SixPlayers();
    match.SetTick(match.Rules().capitalGuardTicks);

    const Frontier::SystemId target = match.GalaxyGraph().Capitals()[0];

    // Player 0 lapses, and their fleet is elsewhere so the capital is undefended.
    match.MutableFleets()[0].at = NeighborOf(match, match.GalaxyGraph().Capitals()[0]);
    for (std::uint32_t tick = 0; tick < match.Rules().custodianAbsenceTicks; ++tick)
    {
      match = WithoutPlayer(match, 0);
    }

    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{1};
    raider.ships = 60;
    raider.at = target;
    (void)match.AddFleet(raider);

    for (std::int32_t tick = 0; tick < 2; ++tick)
    {
      match = WithoutPlayer(match, 0);
    }

    Assert::IsTrue(match.SystemAt(target).owner == Frontier::PlayerId{1}, L"the custodian lost it");
    Assert::IsTrue(match.SystemAt(target).halfYield, L"and it is stamped");

    // A third player takes it from the conqueror. The stamp stays.
    Frontier::MatchFleet second;
    second.owner = Frontier::PlayerId{2};
    second.ships = 200;
    second.at = target;
    (void)match.AddFleet(second);

    for (std::int32_t tick = 0; tick < 4 && !(match.SystemAt(target).owner == Frontier::PlayerId{2}); ++tick)
    {
      match = WithoutPlayer(match, 0);
    }

    Assert::IsTrue(match.SystemAt(target).owner == Frontier::PlayerId{2});
    Assert::IsTrue(match.SystemAt(target).halfYield, L"the infrastructure stays decayed under new ownership");
  }

  // "A player who goes custodian in the first week scores nothing for the match."
  TEST_METHOD(AFirstWeekCustodianScoresNothingEverAfter)
  {
    Frontier::Match match = SixPlayers();
    Assert::IsTrue(match.Tick() < match.Rules().firstWeekTicks);

    for (std::uint32_t tick = 0; tick < match.Rules().custodianAbsenceTicks; ++tick)
    {
      match = WithoutPlayer(match, 0);
    }
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).forfeitedScore);
    Assert::AreEqual(0U, match.PlayerAt(Frontier::PlayerId{0}).score, L"holding a capital and scoring nothing");
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{1}).score > 0U, L"and everybody else scores normally");

    // Coming back does not refund it.
    for (std::int32_t tick = 0; tick < 3; ++tick)
    {
      match = Everyone(match);
    }
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Active);
    Assert::AreEqual(0U, match.PlayerAt(Frontier::PlayerId{0}).score, L"the cost is already incurred");
  }

  TEST_METHOD(ACustodianAfterTheFirstWeekKeepsTheirScore)
  {
    Frontier::Match match = SixPlayers();
    match.SetTick(match.Rules().firstWeekTicks);

    for (std::uint32_t tick = 0; tick < match.Rules().custodianAbsenceTicks; ++tick)
    {
      match = WithoutPlayer(match, 0);
    }

    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).status == Frontier::PlayerStatus::Custodian);
    Assert::IsFalse(match.PlayerAt(Frontier::PlayerId{0}).forfeitedScore);
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).score > 0U);
  }

  // "Four states, six transitions. Nothing else exists" -- and 4X-01 §1 says Exile and Gone are
  // unreachable in Stage A. This is the test that says so out loud, so that the day one becomes
  // reachable it is a deliberate change rather than a surprise.
  TEST_METHOD(ExileAndGoneAreUnreachableInThisStage)
  {
    Frontier::Match match = SixPlayers();
    match.SetTick(match.Rules().capitalGuardTicks);

    // The most violent thing Stage A can do: wipe a player's fleet and take their capital.
    Frontier::MatchFleet executioner;
    executioner.owner = Frontier::PlayerId{1};
    executioner.ships = 500;
    executioner.at = match.GalaxyGraph().Capitals()[0];
    (void)match.AddFleet(executioner);

    for (std::int32_t tick = 0; tick < 6; ++tick)
    {
      match = WithoutPlayer(match, 0);
    }

    Assert::IsTrue(match.SystemAt(match.GalaxyGraph().Capitals()[0]).owner == Frontier::PlayerId{1},
                   L"the capital fell and the fleet is gone");

    for (const Frontier::PlayerState& player : match.Players())
    {
      Assert::IsFalse(player.status == Frontier::PlayerStatus::Exile, L"Exile is not implemented in Stage A");
      Assert::IsFalse(player.status == Frontier::PlayerStatus::Gone, L"nor is Gone");
    }
  }

  TEST_METHOD(EveryPlayerStatusDescribesItself)
  {
    for (std::uint8_t status = 0; status <= static_cast<std::uint8_t>(Frontier::PlayerStatus::Gone); ++status)
    {
      const char* text = Describe(static_cast<Frontier::PlayerStatus>(status));
      Assert::IsNotNull(text);
      Assert::AreNotEqual("unknown", text);
    }
  }
};

TEST_CLASS(ScoreAndEndingTests)
{
public:
  // Recomputed every tick from what is held, never accumulated. That is what keeps a leader
  // attackable -- "public score, the leader is always visible" is the anti-snowball.
  TEST_METHOD(ScoreIsWhatYouHoldNowAndNotWhatYouEverHeld)
  {
    Frontier::Match match = SixPlayers();
    match = Everyone(match);

    const std::uint32_t capitalOnly = match.PlayerAt(Frontier::PlayerId{0}).score;
    Assert::AreEqual(match.Rules().scorePerSystem + match.Rules().capitalScoreBonus, capitalOnly);

    // Take a second system, and it rises.
    const Frontier::SystemId extra = NeighborOf(match, match.GalaxyGraph().Capitals()[0]);
    match.MutableSystems()[extra.AsSize()].owner = Frontier::PlayerId{0};
    match = Everyone(match);
    Assert::AreEqual(capitalOnly + match.Rules().scorePerSystem, match.PlayerAt(Frontier::PlayerId{0}).score);

    // Lose it again, and it falls back. A running total could not do this.
    match.MutableSystems()[extra.AsSize()].owner = Frontier::PlayerId{};
    match = Everyone(match);
    Assert::AreEqual(capitalOnly, match.PlayerAt(Frontier::PlayerId{0}).score);
  }

  TEST_METHOD(TheLeaderIsIdentifiedAndVisibleToEverybody)
  {
    Frontier::Match match = SixPlayers();
    for (std::size_t index = 0; index < match.Systems().size(); ++index)
    {
      if (!match.Systems()[index].owner.IsValid() && index != match.GalaxyGraph().RegionAnchor().AsSize())
      {
        match.MutableSystems()[index].owner = Frontier::PlayerId{4};
      }
    }

    match = Everyone(match);
    Assert::IsTrue(match.Leader() == Frontier::PlayerId{4});
    Assert::IsTrue(match.Placements().front() == Frontier::PlayerId{4});
  }

  // "An early dominance threshold ends the match only if held for several consecutive ticks."
  TEST_METHOD(DominanceEndsTheMatchOnlyWhenHeldLongEnough)
  {
    Frontier::Match match = SixPlayers();
    const std::uint32_t needed = match.Rules().dominanceHoldTicks;

    // Give player 5 everything that is not a capital or the region, which puts them well past the
    // share.
    for (std::size_t index = 0; index < match.Systems().size(); ++index)
    {
      const Frontier::SystemId system{static_cast<std::int32_t>(index)};
      if (match.GalaxyGraph().SystemAt(system).kind != Frontier::SystemKind::RegionAnchor && !match.Systems()[index].owner.IsValid())
      {
        match.MutableSystems()[index].owner = Frontier::PlayerId{5};
      }
    }

    for (std::uint32_t tick = 1; tick < needed; ++tick)
    {
      match = Everyone(match);
      Assert::IsFalse(match.IsFinished(), (std::wstring(L"ended after ") + std::to_wstring(tick) + L" ticks").c_str());
      Assert::AreEqual(tick, match.PlayerAt(Frontier::PlayerId{5}).dominanceTicks);
    }

    Frontier::TickLog log;
    match = Frontier::TickResolver::Resolve(match, {}, log);

    Assert::IsTrue(match.IsFinished(), L"held for the full run");
    Assert::IsTrue(match.DominanceWinner() == Frontier::PlayerId{5});
    Assert::IsTrue(HasDigestKind(log, 0, Frontier::DigestKind::MatchEnded), L"everybody is told");
    Assert::IsTrue(HasDigestKind(log, 5, Frontier::DigestKind::MatchEnded));
  }

  // Consecutive means consecutive. One tick below the share and the count starts again.
  TEST_METHOD(OneTickBelowTheShareResetsTheDominanceCount)
  {
    Frontier::Match match = SixPlayers();
    std::vector<Frontier::SystemId> taken;
    for (std::size_t index = 0; index < match.Systems().size(); ++index)
    {
      const Frontier::SystemId system{static_cast<std::int32_t>(index)};
      if (match.GalaxyGraph().SystemAt(system).kind != Frontier::SystemKind::RegionAnchor && !match.Systems()[index].owner.IsValid())
      {
        match.MutableSystems()[index].owner = Frontier::PlayerId{5};
        taken.push_back(system);
      }
    }

    match = Everyone(match);
    Assert::AreEqual(1U, match.PlayerAt(Frontier::PlayerId{5}).dominanceTicks);

    // Everything goes back to nobody. The share collapses.
    for (const Frontier::SystemId system : taken)
    {
      match.MutableSystems()[system.AsSize()].owner = Frontier::PlayerId{};
    }
    match = Everyone(match);

    Assert::AreEqual(0U, match.PlayerAt(Frontier::PlayerId{5}).dominanceTicks, L"it starts again from nothing");
    Assert::IsFalse(match.IsFinished());
  }

  TEST_METHOD(TheMatchEndsAtTheFixedTick)
  {
    Frontier::MatchRules rules;
    rules.matchLengthTicks = 3;
    Frontier::Match match = SixPlayers(rules);

    match = Everyone(match);
    Assert::IsFalse(match.IsFinished());
    match = Everyone(match);
    Assert::IsFalse(match.IsFinished());

    Frontier::TickLog log;
    match = Frontier::TickResolver::Resolve(match, {}, log);

    Assert::IsTrue(match.IsFinished());
    Assert::IsFalse(match.DominanceWinner().IsValid(), L"it ran out rather than being won early");
    Assert::IsTrue(HasDigestKind(log, 2, Frontier::DigestKind::MatchEnded));
  }

  // ADR-023's tiebreak, and the reason it exists: a placement that depended on sort stability
  // would differ between two machines reporting the same match.
  TEST_METHOD(PlacementsAreTotallyOrderedEvenWhenScoresTie)
  {
    Frontier::Match match = SixPlayers();
    match = Everyone(match);

    // Six identical starts: one capital each, so six identical scores.
    for (std::size_t index = 1; index < match.Players().size(); ++index)
    {
      Assert::AreEqual(match.Players()[0].score, match.Players()[index].score, L"this test needs a six-way tie");
    }

    const std::vector<Frontier::PlayerId> placements = match.Placements();
    Assert::AreEqual(static_cast<size_t>(6), placements.size());
    for (std::size_t index = 0; index < placements.size(); ++index)
    {
      Assert::AreEqual(static_cast<std::int32_t>(index), placements[index].Index(), L"an exact tie falls back on the player id, ascending");
    }

    // And the ordering is stable across two identical matches.
    Frontier::Match second = Everyone(SixPlayers());
    Assert::IsTrue(match.Placements() == second.Placements());
  }

  TEST_METHOD(MoreSystemsBreaksAScoreTie)
  {
    Frontier::Match match = SixPlayers();

    // Player 3 gains a system; player 1 gains a capital elsewhere worth the same total.
    const Frontier::SystemId spare = NeighborOf(match, match.GalaxyGraph().Capitals()[3]);
    match.MutableSystems()[spare.AsSize()].owner = Frontier::PlayerId{3};
    match = Everyone(match);

    const std::vector<Frontier::PlayerId> placements = match.Placements();
    Assert::IsTrue(placements.front() == Frontier::PlayerId{3}, L"more score, first place");
  }

  TEST_METHOD(RulesThatMakeDominanceMeaninglessAreRefused)
  {
    const auto problemWith = [](auto _break)
    {
      Frontier::MatchRules rules;
      _break(rules);
      return Check(rules);
    };

    Assert::IsTrue(problemWith([](Frontier::MatchRules& _r) { _r.dominanceSharePercent = 10; }) ==
                     Frontier::RulesProblem::DominanceUnreachableOrTrivial,
                   L"a share a sixth of the board reaches by existing");
    Assert::IsTrue(problemWith([](Frontier::MatchRules& _r) { _r.dominanceSharePercent = 101; }) ==
                   Frontier::RulesProblem::DominanceUnreachableOrTrivial);
    Assert::IsTrue(problemWith([](Frontier::MatchRules& _r) { _r.dominanceHoldTicks = 0; }) ==
                   Frontier::RulesProblem::DominanceNeedsNoHolding);
  }
};

} // namespace GameLogicTests
