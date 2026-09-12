// ScriptedMatchTests.cpp -- six bots play a whole match.
//
// Step 9 of Design/Plans/4X-01-CoreLoop.md, and the pre-Phase-0 gate. Everything before this tested
// one rule at a time against a state built by hand; this runs all of them together for
// `matchLengthTicks` and checks the invariants on every single tick.
//
// THE BOTS PLAY FROM THE SNAPSHOT, NOT FROM THE MATCH. That is deliberate and it is a second test
// riding on the first: if a policy cannot find something it needs to decide, the snapshot is
// missing something a real client would also be missing. A bot that read the authoritative state
// would prove nothing about what a player can actually see.
//
// They are also entirely deterministic -- every choice breaks ties on the lowest id -- so that a
// difference between two runs is a difference in the simulation rather than in the bots.

#include "pch.h"
#include "CppUnitTest.h"

#include "BotPolicy.h"

#include "Snapshot.h"
#include "TickResolver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint64_t SEED = 0x4652'4F4E'5449'4552ULL;

/// The six the scripted match plays. `BotPolicy` is the game's now (ADR-037) -- this is the roster,
/// and the bots themselves are the shipped ones, so a change to how a bot plays is a change this
/// whole-match run sees.
using Policy = Lockstep::BotPolicy;

constexpr std::array<Policy, 6> POLICIES = {Policy::ExpandNear, Policy::ExpandFar, Policy::Turtle,
                                            Policy::Raider,     Policy::Diplomat,  Policy::Absentee};

constexpr std::int32_t ABSENTEE = 5;

/// Everything that must be true of a match at the end of every tick.
///
/// Returned as a sentence rather than asserted here, so the failure names the tick it happened on
/// -- "invariant violated" on tick 61 of 84 is a bug report nobody can act on.
[[nodiscard]] std::string Violation(const Lockstep::Match& _match)
{
  if (!_match.IsConsistent())
  {
    return "IsConsistent failed";
  }

  for (std::size_t index = 0; index < _match.Fleets().size(); ++index)
  {
    const Lockstep::MatchFleet& fleet = _match.Fleets()[index];
    if (fleet.destroyed)
    {
      continue;
    }

    if (fleet.InTransit())
    {
      // A fleet under way must be on a lane that exists. Anything else is a fleet in open space,
      // which this game does not have -- "not a coordinate map".
      bool onALane = false;
      for (const Lockstep::LaneId lane : _match.GalaxyGraph().LanesAt(fleet.movingFrom))
      {
        if (_match.GalaxyGraph().OtherEnd(lane, fleet.movingFrom) == fleet.movingTo)
        {
          onALane = true;
          break;
        }
      }
      if (!onALane)
      {
        return std::format("fleet {} is in transit along no lane", index);
      }
    }
    else if (!fleet.at.IsValid())
    {
      return std::format("fleet {} is parked nowhere", index);
    }
  }

  for (const Lockstep::OpenProposal& proposal : _match.Proposals())
  {
    if (_match.Tick() > proposal.openedAt + _match.Rules().proposalWindowTicks)
    {
      return std::format("proposal {} outlived its window", proposal.id.Index());
    }
  }

  for (std::size_t index = 0; index < _match.Players().size(); ++index)
  {
    // Credits are unsigned, so an underflow does not go negative -- it goes enormous. That is the
    // shape the check has to have.
    if (_match.Players()[index].credits > 100'000'000U)
    {
      return std::format("player {} credits underflowed", index);
    }
  }

  for (const Lockstep::ActiveTradeLane& lane : _match.TradeLanes())
  {
    const Lockstep::GalaxyLane& edge = _match.GalaxyGraph().LaneAt(lane.lane);
    const Lockstep::PlayerId first = _match.SystemAt(edge.a).owner;
    const Lockstep::PlayerId second = _match.SystemAt(edge.b).owner;
    if (!((first == lane.a && second == lane.b) || (first == lane.b && second == lane.a)))
    {
      return "a trade lane outlived its endpoints";
    }
  }

  return {};
}

struct Played
{
  Lockstep::Match match;
  std::uint32_t ticks = 0;
  /// Highest count seen at any point, since lanes open and close.
  std::size_t mostTradeLanes = 0;
  bool absenteeWentIntoCustody = false;
  std::uint32_t proposalsMade = 0;
  /// The test plan's Phase 0 watch item, summed over the match: how often a fleet was arrived on
  /// top of, and how often it left instead of fighting.
  std::uint32_t timesTargeted = 0;
  std::uint32_t timesDodged = 0;
  std::uint32_t diplomatTouchedARival = 0;
  std::uint32_t absenteeCustodyTick = 0;
  double totalResolveMilliseconds = 0.0;
  std::string violation;
  std::uint32_t violatedAtTick = 0;
};

/// Runs a whole match with the six policies. `_checkInvariants` off makes the second run of the
/// determinism test measure the simulation rather than the checker.
[[nodiscard]] Played PlayAMatch(bool _checkInvariants = true)
{
  Lockstep::MatchRules rules;
  rules.playerCount = 6;

  Played played{.match = Lockstep::Match::Create(rules, SEED)};

  // Everybody but the absentee, every tick. Presence is the server's to report and the harness is
  // standing in for one.
  std::vector<Lockstep::PlayerId> present;
  for (std::int32_t player = 0; player < 6; ++player)
  {
    if (player != ABSENTEE)
    {
      present.emplace_back(player);
    }
  }

  while (!played.match.IsFinished())
  {
    std::vector<Lockstep::OrderSet> orders;
    orders.reserve(POLICIES.size());
    for (std::size_t index = 0; index < POLICIES.size(); ++index)
    {
      const Lockstep::PlayerId player{static_cast<std::int32_t>(index)};
      const Lockstep::Snapshot view = Lockstep::Snapshot::For(played.match, player);
      orders.push_back(Lockstep::BotOrdersFor(POLICIES[index], view, played.match.Rules()));

      if (POLICIES[index] == Policy::Diplomat)
      {
        played.proposalsMade += static_cast<std::uint32_t>(orders.back().proposals.size());
        for (const Lockstep::SnapshotLane& lane : view.Lanes())
        {
          const Lockstep::SnapshotSystem* first = view.System(lane.a);
          const Lockstep::SnapshotSystem* second = view.System(lane.b);
          if (first != nullptr && second != nullptr && first->live && second->live &&
              (first->owner == player) != (second->owner == player) && (first->owner.IsValid() && second->owner.IsValid()))
          {
            ++played.diplomatTouchedARival;
            break;
          }
        }
      }
    }

    Lockstep::TickLog log;
    const auto startedAt = std::chrono::steady_clock::now();
    played.match = Lockstep::TickResolver::Resolve(
      played.match, Lockstep::TickInput{.orders = orders, .present = present, .presenceUnknown = false}, log);
    played.totalResolveMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();

    ++played.ticks;
    played.mostTradeLanes = std::max(played.mostTradeLanes, played.match.TradeLanes().size());
    played.timesTargeted += static_cast<std::uint32_t>(log.interceptions.size());
    played.timesDodged += log.Dodges();

    if (!played.absenteeWentIntoCustody && played.match.PlayerAt(Lockstep::PlayerId{ABSENTEE}).status == Lockstep::PlayerStatus::Custodian)
    {
      played.absenteeWentIntoCustody = true;
      played.absenteeCustodyTick = played.match.Tick();
    }

    if (_checkInvariants && played.violation.empty())
    {
      played.violation = Violation(played.match);
      played.violatedAtTick = played.match.Tick();
    }

    // A guard against the harness itself looping, distinct from the match not ending.
    if (played.ticks > played.match.Rules().matchLengthTicks + 10)
    {
      break;
    }
  }

  return played;
}

} // namespace

// The pre-Phase-0 gate.
TEST_CLASS(ScriptedMatchTests)
{
public:
  TEST_METHOD(SixBotsPlayAMatchToTheEnd)
  {
    const Played played = PlayAMatch();

    Assert::IsTrue(played.violation.empty(), (std::wstring(L"tick ") + std::to_wstring(played.violatedAtTick) + L": " +
                                              std::wstring(played.violation.begin(), played.violation.end()))
                                               .c_str());

    Assert::IsTrue(played.match.IsFinished(), L"the match ended");
    Assert::IsTrue(played.ticks <= played.match.Rules().matchLengthTicks, L"and ended on schedule or early, not by the harness giving up");
    Assert::IsTrue(played.match.IsConsistent());

    Logger::WriteMessage(std::format("scripted match: {} ticks, ended {}\n", played.ticks,
                                     played.match.DominanceWinner().IsValid() ? "by dominance" : "at the fixed tick")
                           .c_str());
  }

  // The test plan's Phase 0 watch item, over a whole match rather than a contrived tick:
  //
  // > If a fleet escapes this way more than a third of the time it is targeted, and the dodging
  // > player retains or retakes the system, enable the rear-guard round and re-run.
  //
  // This asserts that the measurement is *collectable*, not that it is below the threshold. These
  // bots do not dance on purpose and a real Phase 0 is six humans who might; whether the fraction
  // crosses a third is a finding for the owner, not a thing the build should fail on.
  TEST_METHOD(TheDefenderDancingWatchItemIsCollectable)
  {
    const Played played = PlayAMatch();

    const double fraction = played.timesTargeted > 0 ? static_cast<double>(played.timesDodged) / played.timesTargeted : 0.0;

    Logger::WriteMessage(std::format("defender dancing: {} dodges of {} times targeted ({:.0f}%), rear guard {}\n", played.timesDodged,
                                     played.timesTargeted, fraction * 100.0, Lockstep::MatchRules{}.rearGuardEnabled ? "on" : "off")
                           .c_str());

    Assert::IsTrue(played.timesTargeted > 0, L"a match in which nobody was ever arrived on top of would make the watch item unmeasurable");
    Assert::IsTrue(played.timesDodged <= played.timesTargeted, L"the dodges are a subset of the times targeted");
    Assert::IsFalse(Lockstep::MatchRules{}.rearGuardEnabled, L"and it is measured with the round off, which is the state Phase 0 measures");
  }

  // ADR-018 at full scale. Everything before this proved one tick reproducible; this proves
  // eighty-four of them, with six policies reading snapshots and writing orders in between.
  TEST_METHOD(TheWholeMatchIsReproducible)
  {
    const Played first = PlayAMatch(false);
    const Played second = PlayAMatch(false);

    Assert::AreEqual(first.match.Hash(), second.match.Hash(), L"same seed, same bots, same match");
    Assert::AreEqual(first.ticks, second.ticks);
    Assert::AreEqual(first.match.Tick(), second.match.Tick());

    for (std::size_t index = 0; index < first.match.Players().size(); ++index)
    {
      Assert::AreEqual(first.match.Players()[index].score, second.match.Players()[index].score);
    }
  }

  // ADR-018 across builds, not only within one. The test above compares two runs of the same
  // binary, which cannot see a compiler, a standard library or a configuration disagreeing about
  // the same sum. This pins the number.
  //
  // **The value is pinned under MSVC, Debug and Release, and both agree** (re-pinned 2026-09-12
  // when ADR-055 raised `startingCredits`; the value before it was computed with clang 18 and
  // libstdc++ 13 on Linux and agreed with both MSVC configurations, which is the evidence that
  // this simulation does not depend on a toolchain). If this fails and nothing in `GameLogic` was
  // meant to change a rule, the failure IS the finding: two builds disagree about an integer
  // simulation, which means undefined or unspecified behavior somewhere in it. If a rule was meant
  // to change, re-pin -- and know that every stored match is now unloadable (ADR-024).
  TEST_METHOD(TheWholeMatchHashIsPinnedAcrossToolchains)
  {
    constexpr std::uint64_t PINNED_HASH = 0xC204BAED2104E2E7ULL;
    constexpr std::uint64_t PINNED_SEED = 0xC7920238303AD5D8ULL;

    const Played played = PlayAMatch(false);

    Assert::AreEqual(PINNED_SEED, played.match.Seed(), L"the generator accepted a different seed: the galaxy itself has changed");
    Assert::AreEqual(84U, played.ticks, L"the match ran a different number of ticks");
    Assert::AreEqual(PINNED_HASH, played.match.Hash(), L"the same seed and the same bots must produce this match on every toolchain");
  }

  // The absentee never logs in, so custody should arrive on the tick the rule says and not later.
  TEST_METHOD(TheAbsenteeGoesIntoCustodyOnSchedule)
  {
    const Played played = PlayAMatch();

    Assert::IsTrue(played.absenteeWentIntoCustody, L"three weeks away and still active would be a broken rule");
    Assert::AreEqual(played.match.Rules().custodianAbsenceTicks, played.absenteeCustodyTick, L"on the tick the rule names, not eventually");
    Assert::IsTrue(played.match.PlayerAt(Lockstep::PlayerId{ABSENTEE}).forfeitedScore, L"and inside the first week, so they score nothing");
    Assert::AreEqual(0U, played.match.PlayerAt(Lockstep::PlayerId{ABSENTEE}).score);
  }

  // The diplomat's whole policy is to open lanes. If none ever opened, either the policy cannot see
  // what it needs in the snapshot or the lifecycle does not work end to end.
  TEST_METHOD(TheDiplomatsLanesOpenAndPay)
  {
    const Played played = PlayAMatch();

    Logger::WriteMessage(std::format("trade lanes open at peak: {}, proposals made: {}, ticks touching a rival: {}\n",
                                     played.mostTradeLanes, played.proposalsMade, played.diplomatTouchedARival)
                           .c_str());

    // Asserted in the order the lifecycle happens, so a failure names the step that broke rather
    // than only the end of it. The first of these is what caught the harness bug: with bots that
    // looked one lane ahead, the diplomat never reached anybody and the missing lanes looked like
    // the trade-lane mechanic being broken.
    Assert::IsTrue(played.diplomatTouchedARival > 0, L"the diplomat has to reach somebody before it can offer anything");
    Assert::IsTrue(played.proposalsMade > 0, L"and make an offer before one can be accepted");
    Assert::IsTrue(played.mostTradeLanes > 0, L"at least one lane opened during the match");

    constexpr std::int32_t DIPLOMAT = 4;
    Assert::IsTrue(played.match.PlayerAt(Lockstep::PlayerId{DIPLOMAT}).credits > 0U, L"and the diplomat has income to show for it");
  }

  // Something has to be happening. A match where nobody expands is a match where the harness is
  // driving nothing and every assertion above passes vacuously.
  TEST_METHOD(TheGalaxyIsFoughtOverRatherThanIgnored)
  {
    const Played played = PlayAMatch();

    std::uint32_t claimed = 0;
    for (const Lockstep::SystemState& system : played.match.Systems())
    {
      if (system.owner.IsValid())
      {
        ++claimed;
      }
    }

    Assert::IsTrue(claimed > 6U, L"somebody expanded past their capital");

    std::uint32_t withBuildings = 0;
    for (const Lockstep::SystemState& system : played.match.Systems())
    {
      if (system.hasShipyard || system.hasMiningStation)
      {
        ++withBuildings;
      }
    }
    Assert::IsTrue(withBuildings > 0U, L"and somebody built something");

    Logger::WriteMessage(
      std::format("{} of {} systems claimed, {} built on\n", claimed, played.match.GalaxyGraph().SystemCount(), withBuildings).c_str());
  }

  // §3's persistence recommendation -- a match stored as a seed and a list of order sets, reloaded
  // by re-resolving -- only works if re-resolving is cheap. This is the measurement that decision
  // rests on, and it is recorded in Design/Reference/tick-resolution-cost.md.
  TEST_METHOD(ATickResolvesFastEnoughToReplayAWholeMatch)
  {
    const Played played = PlayAMatch(false);
    const double perTick = played.totalResolveMilliseconds / played.ticks;

    Logger::WriteMessage(std::format("resolution: {:.3f} ms/tick over {} ticks, {:.1f} ms for the match\n", perTick, played.ticks,
                                     played.totalResolveMilliseconds)
                           .c_str());

    // A deliberately loose bound. The point is not to pin a number -- a Debug build on a laptop is
    // not a server -- but to fail if a tick ever becomes so expensive that replaying a whole match
    // stops being a reasonable way to load one.
    Assert::IsTrue(perTick < 50.0, L"a tick must stay cheap enough that re-resolving a match is viable");
    Assert::IsTrue(played.totalResolveMilliseconds < 2000.0, L"and a whole match must replay in about a second");
  }
};

} // namespace GameLogicTests
