// InstrumentationTests.cpp -- what the game narrates, and the rules Phase 0 is played under.
//
// ADR-030: the game emits instrumentation and the server writes it down. These tests are the game's
// half. They drive `MatchSimulation` through the byte seam rather than calling `TickResolver`
// directly, because the thing being tested is what a server would actually get -- and the server
// gets bytes.
//
// WHY THIS IS WORTH TESTING AT ALL: every Phase 0 hypothesis is answered from these lines. A
// hypothesis whose line is never emitted does not fail loudly; it comes back as a clean result
// saying the mechanic was not used. That failure mode is the reason for the file.

#include "pch.h"
#include "CppUnitTest.h"

#include "ByteWriter.h"
#include "MatchRules.h"
#include "MatchSimulation.h"
#include "Orders.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// The same seed the scripted match uses, so a galaxy that behaves oddly here behaves oddly there
/// too rather than being a second unexplained world.
constexpr std::uint64_t INSTRUMENTATION_SEED = 0x5350'4143'4520'3458ULL;

[[nodiscard]] std::vector<std::uint8_t> Encoded(const Lockstep::OrderSet& _orders)
{
  Neuron::ByteWriter writer;
  _orders.Write(writer);
  return writer.Bytes();
}

/// True if any line carries the given event name. The name is matched with its surrounding spaces,
/// so `custodian` does not also match a detail string that happens to contain the word.
[[nodiscard]] bool Mentions(const std::vector<std::string>& _events, const std::string& _event)
{
  const std::string needle = " " + _event + " ";
  return std::ranges::any_of(_events, [&needle](const std::string& _line) { return _line.find(needle) != std::string::npos; });
}

/// Every line starts `T<tick> `. The tick is what lines an event up against a replay, and it is the
/// one field a person reading the log cannot reconstruct from the wall clock beside it.
[[nodiscard]] bool CarriesATick(const std::string& _line)
{
  return _line.size() > 2 && _line[0] == 'T' && std::isdigit(static_cast<unsigned char>(_line[1])) != 0;
}

/// One tick, with everybody present except the named absentee. Returns what was narrated.
[[nodiscard]] std::vector<std::string> ResolveWith(Lockstep::MatchSimulation& _game, std::int32_t _absentee)
{
  for (std::int32_t player = 0; player < _game.PlayerCount(); ++player)
  {
    if (player != _absentee)
    {
      _game.MarkPresent(player);
    }
  }
  _game.Resolve();
  return _game.TakeEvents();
}

} // namespace

TEST_CLASS(PhaseZeroRulesTests)
{
public:
  TEST_METHOD(ThePhaseZeroSetupIsAPlayableGame)
  {
    const Lockstep::MatchRules rules = Lockstep::PhaseZeroRules();

    Assert::IsTrue(Check(rules) == Lockstep::RulesProblem::None, L"the rehearsal has to pass the same check as the real thing");
    Assert::AreEqual(6U, rules.playerCount);
    Assert::AreEqual(3600U, rules.tickIntervalSeconds, L"an hourly tick, so forty-eight of them is a weekend");
    Assert::AreEqual(48U, rules.matchLengthTicks);
  }

  // The three that do not survive being compressed. Left at their authored values, `firstWeekTicks`
  // would cover more than half the match and `regionOpensAtTick` would never arrive at all -- so
  // Phase 0 would be measuring a game nobody designed.
  TEST_METHOD(TheCompressedRulesStillMeanWhatTheyMeanAtFullLength)
  {
    const Lockstep::MatchRules full;
    const Lockstep::MatchRules phaseZero = Lockstep::PhaseZeroRules();

    Assert::IsTrue(phaseZero.firstWeekTicks < phaseZero.matchLengthTicks / 2,
                   L"a first week that is most of the match is not a first week");
    Assert::IsTrue(phaseZero.regionOpensAtTick < phaseZero.matchLengthTicks, L"a region that opens after the end never opens");
    Assert::IsTrue(phaseZero.capitalGuardTicks > 0, L"a capital with no guard is a capital lost on tick one");

    // The same fractions at both lengths. This is the property that lets a number tuned in Phase 0
    // be carried back to a real match rather than re-guessed there.
    Assert::AreEqual(full.firstWeekTicks * phaseZero.matchLengthTicks / full.matchLengthTicks, phaseZero.firstWeekTicks);
    Assert::AreEqual(full.regionOpensAtTick * phaseZero.matchLengthTicks / full.matchLengthTicks, phaseZero.regionOpensAtTick);
  }

  // The one constant that scales by the clock instead of by the match, because absence is about a
  // person's day rather than about how far through the game they are.
  //
  // A compressed rehearsal is what found this: three ticks is eighteen hours at the authored
  // interval and three hours at Phase 0's, so all six players were custodians by T3, all six had
  // forfeited inside the first week, and all six finished on nothing. A weekend measured that way
  // answers no hypothesis at all.
  TEST_METHOD(AbsenceIsMeasuredInHoursRatherThanInTicks)
  {
    const Lockstep::MatchRules full;
    const Lockstep::MatchRules phaseZero = Lockstep::PhaseZeroRules();

    Assert::AreEqual(full.custodianAbsenceTicks * full.tickIntervalSeconds, phaseZero.custodianAbsenceTicks * phaseZero.tickIntervalSeconds,
                     L"the same wall-clock absence at both tick rates");

    constexpr std::uint32_t A_NIGHTS_SLEEP = 8 * 60 * 60;
    Assert::IsTrue(phaseZero.custodianAbsenceTicks * phaseZero.tickIntervalSeconds > A_NIGHTS_SLEEP,
                   L"a player who slept has not stopped playing");

    // The consequence, asserted rather than left to be discovered during a run: at Phase 0's length
    // the first-week forfeit cannot fire, because custody cannot begin before the first week ends.
    // That is the right trade -- a forfeit nobody can trigger beats one everybody triggers on night
    // one -- and it means H2 measures whether people lapse, not what a lapse costs.
    Assert::IsTrue(phaseZero.custodianAbsenceTicks > phaseZero.firstWeekTicks);
  }
};

TEST_CLASS(InstrumentationTests)
{
public:
  TEST_METHOD(ANewMatchHasNarratedNothing)
  {
    Lockstep::MatchSimulation game{Lockstep::PhaseZeroRules(), INSTRUMENTATION_SEED};
    Assert::IsTrue(game.TakeEvents().empty(), L"nothing has happened yet");
  }

  // Taken and cleared, which matters more than it sounds: a log that re-wrote its backlog every
  // tick would report a mechanic as used forty-eight times when it was used once.
  TEST_METHOD(EventsAreTakenAndCleared)
  {
    Lockstep::MatchSimulation game{Lockstep::PhaseZeroRules(), INSTRUMENTATION_SEED};

    const std::vector<std::string> first = ResolveWith(game, -1);
    Assert::IsTrue(game.TakeEvents().empty(), L"a second take must find nothing");

    for (const std::string& line : first)
    {
      Assert::IsTrue(CarriesATick(line), L"every line says which tick it is about");
    }
  }

  // A player who never appears goes into custody, and the log says so. That is the H2 measurement --
  // whether the absence rules are what people actually experience -- and it is reachable without
  // firing a shot, which is why it is the one asserted here.
  TEST_METHOD(AnAbsentPlayerBecomesACustodianInTheLog)
  {
    const Lockstep::MatchRules rules = Lockstep::PhaseZeroRules();
    Lockstep::MatchSimulation game{rules, INSTRUMENTATION_SEED};

    constexpr std::int32_t ABSENTEE = 4;
    std::vector<std::string> everything;

    for (std::uint32_t tick = 0; tick < rules.custodianAbsenceTicks + 2; ++tick)
    {
      const std::vector<std::string> events = ResolveWith(game, ABSENTEE);
      everything.insert(everything.end(), events.begin(), events.end());
    }

    Assert::IsTrue(Mentions(everything, "custodian"), L"the absence rule has to be visible in the instrumentation");

    // Exactly one line, naming the absentee. Every player is told about a custodian, so the digest
    // this is read from carries the event six times -- and a log that wrote all six would report
    // six custodians in a match that had one, five of them the wrong player. That is what this
    // assertion is guarding, and it caught it.
    const auto custodians =
      std::ranges::count_if(everything, [](const std::string& _line) { return _line.find(" custodian ") != std::string::npos; });
    Assert::AreEqual(std::ptrdiff_t{1}, custodians, L"one absentee is one custodian, however many people were told");

    const auto custodian =
      std::ranges::find_if(everything, [](const std::string& _line) { return _line.find(" custodian ") != std::string::npos; });
    Assert::IsTrue(custodian != everything.end());
    Assert::IsTrue(custodian->find("player=4") != std::string::npos, L"and it has to name the absentee, not whoever was told");
  }

  // Coming back is the other half of H2, and it is a different line. It was briefly the same one,
  // which would have made a player who lapsed and returned look like two lapses.
  TEST_METHOD(ComingBackIsNarratedAsTheOppositeEvent)
  {
    const Lockstep::MatchRules rules = Lockstep::PhaseZeroRules();
    Lockstep::MatchSimulation game{rules, INSTRUMENTATION_SEED};

    constexpr std::int32_t LAPSED = 2;
    std::vector<std::string> everything;

    for (std::uint32_t tick = 0; tick < rules.custodianAbsenceTicks + 1; ++tick)
    {
      const std::vector<std::string> events = ResolveWith(game, LAPSED);
      everything.insert(everything.end(), events.begin(), events.end());
    }

    const std::vector<std::string> returned = ResolveWith(game, -1);
    everything.insert(everything.end(), returned.begin(), returned.end());

    Assert::IsTrue(Mentions(everything, "custodian-ended"), L"a player who logs back in has to be visible as a return");

    const auto ended =
      std::ranges::find_if(everything, [](const std::string& _line) { return _line.find(" custodian-ended ") != std::string::npos; });
    Assert::IsTrue(ended != everything.end());
    Assert::IsTrue(ended->find("player=2") != std::string::npos);
  }

  // The match ending is narrated, for every player, with the score that decided it. It closes a
  // Phase 0 log and is the line a person reads first.
  TEST_METHOD(TheEndOfTheMatchIsNarratedForEveryPlayer)
  {
    // A short match rather than the full forty-eight, and scaled the same way `PhaseZeroRules` is,
    // so the test measures the narration rather than the length.
    Lockstep::MatchRules rules = Lockstep::PhaseZeroRules();
    rules.matchLengthTicks = 8;
    rules.firstWeekTicks = 2;
    rules.regionOpensAtTick = 5;
    rules.capitalGuardTicks = 1;
    Assert::IsTrue(Check(rules) == Lockstep::RulesProblem::None);

    Lockstep::MatchSimulation game{rules, INSTRUMENTATION_SEED};
    std::vector<std::string> everything;

    while (!game.IsFinished())
    {
      const std::vector<std::string> events = ResolveWith(game, -1);
      everything.insert(everything.end(), events.begin(), events.end());
    }

    const auto ended =
      std::ranges::count_if(everything, [](const std::string& _line) { return _line.find(" match-ended ") != std::string::npos; });
    Assert::AreEqual(static_cast<std::ptrdiff_t>(rules.playerCount), ended,
                     L"every player is told the match ended, so every one is logged");
  }

  // A submission the seam refuses narrates nothing at all. It is counted instead, because a
  // malformed record is a client bug rather than a thing that happened in the game -- and an event
  // line for it would put a fact about a socket into a file that is measuring a mechanic.
  TEST_METHOD(ARefusedSubmissionIsCountedRatherThanNarrated)
  {
    Lockstep::MatchSimulation game{Lockstep::PhaseZeroRules(), INSTRUMENTATION_SEED};

    game.Submit(0, std::vector<std::uint8_t>{0xFF, 0xFF, 0xFF});
    Assert::AreEqual(std::uint32_t{1}, game.RejectedSubmissions());

    // A well-formed set claiming to be somebody else is refused the same way, and this is the one
    // that matters: accepted, it would replace another player's orders.
    Lockstep::OrderSet impostor;
    impostor.player = Lockstep::PlayerId{3};
    game.Submit(0, Encoded(impostor));
    Assert::AreEqual(std::uint32_t{2}, game.RejectedSubmissions());

    Assert::IsTrue(game.TakeEvents().empty());
  }
};

} // namespace GameLogicTests
