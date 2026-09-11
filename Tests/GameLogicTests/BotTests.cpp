// BotTests.cpp -- the seats that play themselves.
//
// ADR-037. `ScriptedMatchTests.cpp` already drives these policies through a whole match and is the
// evidence that they reach the game's mechanics; this file is about the two things that became true
// when the bots stopped being a test harness and became a feature:
//
//   1. A bot seat plays WITHOUT ANYBODY SUBMITTING FOR IT. That is the whole feature, and it is the
//      one thing the scripted match cannot show, because there the harness does the submitting.
//   2. A bot roster SURVIVES THE STORE. A match reloaded without its roster would have live seats
//      nobody plays -- they would go absent, the custodian rules would fire, and the reloaded match
//      would diverge from the recorded one for a reason nothing wrote down.

#include "pch.h"
#include "CppUnitTest.h"

#include "BotPolicy.h"
#include "MatchRules.h"
#include "MatchSimulation.h"
#include "Snapshot.h"

#include <cstdint>
#include <optional>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint64_t BOT_SEED = 0x424F'5453'4545'4421ULL;

/// Six seats, all bots, all of them the same style unless asked otherwise.
[[nodiscard]] std::vector<std::optional<Lockstep::BotPolicy>> AllBots(Lockstep::BotPolicy _policy = Lockstep::BotPolicy::ExpandNear)
{
  return std::vector<std::optional<Lockstep::BotPolicy>>(6, std::optional<Lockstep::BotPolicy>{_policy});
}

} // namespace

TEST_CLASS(BotTests)
{
public:
  TEST_METHOD(ABotSeatPlaysWithoutAnybodySubmitting)
  {
    // Nothing is submitted and nobody is marked present. A match of humans would stall here, every
    // seat absent; a match of bots has to advance.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    Lockstep::MatchSimulation simulation{rules, BOT_SEED, AllBots()};

    const std::uint64_t before = simulation.Hash();
    simulation.Resolve();

    Assert::AreEqual(1U, simulation.Tick(), L"the tick did not advance");
    Assert::AreNotEqual(before, simulation.Hash(), L"nothing in the match changed, so nobody played");
    Assert::AreEqual(0U, simulation.RejectedSubmissions(), L"the bot's own orders were refused as malformed");
  }

  TEST_METHOD(ABotIsNeverAbsent)
  {
    // The custodian rules exist for a person who stopped turning up. A seat that plays itself
    // cannot be that, and the locked turn is where the server would otherwise read absence.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    Lockstep::MatchSimulation simulation{rules, BOT_SEED, AllBots()};
    simulation.Resolve();

    const std::vector<Neuron::PlayerTurn> locked = simulation.LockedTurn();
    Assert::AreEqual(std::size_t{6}, locked.size());
    for (const Neuron::PlayerTurn& turn : locked)
    {
      Assert::IsTrue(turn.present, L"a bot seat was recorded absent");
      Assert::IsFalse(turn.orders.empty(), L"a bot seat locked no orders");
    }
  }

  TEST_METHOD(AHumansOrdersBeatTheBotsOnTheSameSeat)
  {
    // Whatever arrived first is what is played. The seat is nominally a bot's, but a submission is
    // a person at a keyboard and the bot must not overwrite them.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    Lockstep::MatchSimulation simulation{rules, BOT_SEED, AllBots()};

    Lockstep::OrderSet mine;
    mine.player = Lockstep::PlayerId{0};
    Neuron::ByteWriter writer;
    mine.Write(writer);
    simulation.Submit(0, writer.Bytes());
    simulation.Resolve();

    Assert::AreEqual(writer.Bytes().size(), simulation.LockedTurn()[0].orders.size(), L"the bot overwrote a submitted order set");
  }

  TEST_METHOD(AHumanSeatStillNeedsAHuman)
  {
    // The negative of the first test, and the one that says `PlayBots` is reading the roster rather
    // than playing everybody.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    std::vector<std::optional<Lockstep::BotPolicy>> roster = AllBots();
    roster[2].reset();

    Lockstep::MatchSimulation simulation{rules, BOT_SEED, roster};
    simulation.Resolve();

    const std::vector<Neuron::PlayerTurn> locked = simulation.LockedTurn();
    Assert::IsFalse(locked[2].present, L"a seat with nobody in it was reported present");
    Assert::IsTrue(locked[2].orders.empty(), L"something played a human's seat");
    Assert::IsTrue(locked[1].present, L"the bot beside it stopped playing");
  }

  TEST_METHOD(TheRosterSurvivesTheConfiguration)
  {
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    std::vector<std::optional<Lockstep::BotPolicy>> roster = AllBots(Lockstep::BotPolicy::Raider);
    roster[0].reset();
    roster[4] = Lockstep::BotPolicy::Turtle;

    Lockstep::MatchSimulation original{rules, BOT_SEED, roster};
    Lockstep::MatchSimulation reloaded = Lockstep::MatchSimulation::FromConfiguration(original.Configuration());

    Assert::AreEqual(original.Hash(), reloaded.Hash(), L"the reloaded match started from a different galaxy");

    // Played rather than inspected, because the roster has no accessor and does not need one: what
    // it is for is which seats move, so that is what is asserted.
    original.Resolve();
    reloaded.Resolve();
    Assert::AreEqual(original.Hash(), reloaded.Hash(), L"the reloaded match played a different tick");

    const std::vector<Neuron::PlayerTurn> locked = reloaded.LockedTurn();
    Assert::IsTrue(locked[0].orders.empty(), L"the human seat came back as a bot");
    Assert::IsFalse(locked[4].orders.empty(), L"a bot seat came back empty");
  }

  TEST_METHOD(AStoreWrittenBeforeBotsExistedReloadsAsAllHuman)
  {
    // The roster is appended after the seed, so a configuration that ends at the seed is one from
    // an earlier build. It has to reload as the match it recorded, which is one with no bots.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    const Lockstep::MatchSimulation withBots{rules, BOT_SEED, AllBots()};

    std::vector<std::uint8_t> configuration = withBots.Configuration();
    configuration.resize(configuration.size() - rules.playerCount);

    Lockstep::MatchSimulation reloaded = Lockstep::MatchSimulation::FromConfiguration(configuration);
    reloaded.Resolve();

    for (const Neuron::PlayerTurn& turn : reloaded.LockedTurn())
    {
      Assert::IsTrue(turn.orders.empty(), L"an old store grew bots it never had");
    }
  }

  TEST_METHOD(TwoRunsOfTheSameRosterAgree)
  {
    // R16, asked of the bots specifically. They are the one part of the game that decides rather
    // than resolves, so a clock or an unordered walk hiding in a policy would show up here.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    Lockstep::MatchSimulation first{rules, BOT_SEED, AllBots(Lockstep::BotPolicy::Raider)};
    Lockstep::MatchSimulation second{rules, BOT_SEED, AllBots(Lockstep::BotPolicy::Raider)};

    for (std::int32_t tick = 0; tick < 12; ++tick)
    {
      first.Resolve();
      second.Resolve();
      Assert::AreEqual(first.Hash(), second.Hash(), L"two identical bot matches diverged");
    }
  }

  TEST_METHOD(EveryOfferedStyleActuallyDoesSomething)
  {
    // A style that never issues an order would look exactly like a working bot from the outside:
    // the seat is present, the tick resolves, and the empire simply never grows. The seats screen
    // offers three and each of them has to be a different game to play against.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;

    std::vector<std::uint64_t> hashes;
    for (const Lockstep::BotPolicy policy : {Lockstep::BotPolicy::Turtle, Lockstep::BotPolicy::ExpandNear, Lockstep::BotPolicy::Raider})
    {
      Lockstep::MatchSimulation simulation{rules, BOT_SEED, AllBots(policy)};
      for (std::int32_t tick = 0; tick < 20; ++tick)
      {
        simulation.Resolve();
      }
      hashes.push_back(simulation.Hash());
    }

    Assert::AreNotEqual(hashes[0], hashes[1], L"CAUTIOUS and STEADY play the same match");
    Assert::AreNotEqual(hashes[1], hashes[2], L"STEADY and AGGRESSIVE play the same match");
  }

  TEST_METHOD(EveryPolicyHasAWordForIt)
  {
    // `Describe` is on a seat card, so a policy without a word in it ships as "unknown" on screen.
    for (const Lockstep::BotPolicy policy : {Lockstep::BotPolicy::Turtle, Lockstep::BotPolicy::ExpandNear, Lockstep::BotPolicy::ExpandFar,
                                             Lockstep::BotPolicy::Raider, Lockstep::BotPolicy::Diplomat, Lockstep::BotPolicy::Absentee})
    {
      Assert::AreNotEqual("unknown", Lockstep::Describe(policy));
    }
  }
};

} // namespace GameLogicTests
