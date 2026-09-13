// BalanceProbeTests.cpp -- ten bot matches, and what the numbers say about the settings.
//
// **AN INSTRUMENT, NOT A GATE.** It plays whole matches under the authored rules and under
// `PhaseZeroRules` and PRINTS what a playtest would measure: how long credits stay interesting, how
// much of the board is claimed by day five, whether territory ever changes hands again, what the
// bots build, and what the lock refuses them. It asserts no balance figure on purpose -- a balance
// figure is Phase 0's to choose, and a test that pinned one would have to be edited every time
// somebody tuned a number, which is how a gate becomes a thing people edit until it is quiet.
//
// It is a test because the only way to play a real match is to link `GameLogic`, and the only thing
// in this tree that links `GameLogic` and can be run on demand is a test project. Read the numbers
// in the run's `.trx`, or run just these two:
//
//     vstest.console.exe x64\Debug\GameLogicTests.dll /Platform:x64 ^
//       /Tests:TenMatchesUnderTheAuthoredRules,TenMatchesUnderPhaseZeroRules
//
// **What it found on 2026-09-13**, recorded in `Design/blueprint.md` §8 because a number nobody
// wrote down is a number somebody measures again: 115 sieges and zero captures across twenty
// matches; credits dead from about tick 42 of 84; and ten different galaxies producing near
// identical matches, which is why bot output is evidence about the rules and almost none about
// variance.

#include "pch.h"
#include "CppUnitTest.h"

#include "BotPolicy.h"
#include "Snapshot.h"
#include "TickResolver.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Policy = Lockstep::BotPolicy;

constexpr std::array<Policy, 6> POLICIES = {Policy::ExpandNear, Policy::ExpandFar, Policy::Turtle,
                                            Policy::Raider,     Policy::Diplomat,  Policy::Absentee};
constexpr std::int32_t ABSENTEE = 5;

struct Probe
{
  std::uint64_t acceptedSeed = 0;
  std::uint32_t ticks = 0;
  std::uint32_t systems = 0;

  std::uint32_t creditsQuarter = 0;
  std::uint32_t creditsHalf = 0;
  std::uint32_t creditsEnd = 0;
  /// The tick after which nobody started a building again. Early means credits are dead.
  std::uint32_t lastBuildStartedAt = 0;
  std::uint32_t buildsStarted = 0;
  /// How many buildings stand at each level at the end, indexed by level.
  std::array<std::uint32_t, 4> levelsHeld = {};

  std::uint32_t claimedAtDayFive = 0;
  std::uint32_t claimedAtEnd = 0;
  /// Ownership taken from somebody after the board was first carved up. A claim of empty ground
  /// carries no previous owner, which is how the two are told apart.
  std::uint32_t capturesAfterDayFive = 0;
  std::uint32_t siegesBegun = 0;
  std::uint32_t capitalsLost = 0;

  std::uint32_t leaderScore = 0;
  std::uint32_t medianScore = 0;
  std::uint32_t lastScore = 0;
  std::uint32_t leaderSharePercent = 0;

  std::uint32_t battles = 0;
  std::uint32_t mostTradeLanes = 0;
  std::uint32_t proposalsIgnored = 0;
  std::uint32_t ordersRefused = 0;
  std::array<std::uint32_t, 20> refusalReasons = {};
};

[[nodiscard]] std::uint32_t CountKind(const Lockstep::TickLog& _log, Lockstep::DigestKind _kind)
{
  std::uint32_t seen = 0;
  for (const std::vector<Lockstep::DigestEntry>& digest : _log.digests)
  {
    for (const Lockstep::DigestEntry& entry : digest)
    {
      seen += entry.kind == _kind ? 1U : 0U;
    }
  }
  return seen;
}

[[nodiscard]] Probe PlayOne(std::uint64_t _seed, Lockstep::MatchRules _rules)
{
  _rules.playerCount = 6;

  Probe probe;
  Lockstep::Match match = Lockstep::Match::Create(_rules, _seed);
  probe.systems = static_cast<std::uint32_t>(match.Systems().size());
  probe.acceptedSeed = match.Seed();

  // The blueprint's "fully claimed by roughly day five": four ticks a day, so day five is tick 20
  // of an 84-tick match, and a quarter of the way through a shorter preset.
  const std::uint32_t dayFive = std::min(20U, _rules.matchLengthTicks / 4U);

  std::vector<Lockstep::PlayerId> present;
  for (std::int32_t player = 0; player < 6; ++player)
  {
    if (player != ABSENTEE)
    {
      present.emplace_back(player);
    }
  }

  while (!match.IsFinished() && probe.ticks <= _rules.matchLengthTicks + 4)
  {
    std::vector<Lockstep::OrderSet> orders;
    for (std::size_t index = 0; index < POLICIES.size(); ++index)
    {
      const Lockstep::PlayerId player{static_cast<std::int32_t>(index)};
      orders.push_back(Lockstep::BotOrdersFor(POLICIES[index], Lockstep::Snapshot::For(match, player), match.Rules()));
    }

    // Asked of the same state the resolver is about to validate against, so this counts what the
    // lock actually refused rather than guessing at it.
    for (const Lockstep::OrderSet& set : orders)
    {
      for (const Lockstep::RejectedOrder& refusal : match.Validate(set))
      {
        const std::size_t reason = static_cast<std::size_t>(refusal.reason);
        ++probe.ordersRefused;
        if (reason < probe.refusalReasons.size())
        {
          ++probe.refusalReasons[reason];
        }
      }
    }

    Lockstep::TickLog log;
    match =
      Lockstep::TickResolver::Resolve(match, Lockstep::TickInput{.orders = orders, .present = present, .presenceUnknown = false}, log);
    ++probe.ticks;

    const std::uint32_t started = CountKind(log, Lockstep::DigestKind::BuildStarted);
    probe.buildsStarted += started;
    probe.battles += CountKind(log, Lockstep::DigestKind::Battle) / 2U;
    probe.proposalsIgnored += CountKind(log, Lockstep::DigestKind::ProposalIgnored);
    probe.siegesBegun += CountKind(log, Lockstep::DigestKind::SiegeBegun);
    if (started > 0)
    {
      probe.lastBuildStartedAt = match.Tick();
    }

    if (match.Tick() > dayFive)
    {
      for (const std::vector<Lockstep::DigestEntry>& digest : log.digests)
      {
        for (const Lockstep::DigestEntry& entry : digest)
        {
          probe.capturesAfterDayFive += entry.kind == Lockstep::DigestKind::SystemClaimed && entry.other.IsValid() ? 1U : 0U;
        }
      }
    }

    std::uint32_t purse = 0;
    for (const Lockstep::PlayerState& player : match.Players())
    {
      purse += player.credits;
    }
    if (match.Tick() == _rules.matchLengthTicks / 4U)
    {
      probe.creditsQuarter = purse;
    }
    if (match.Tick() == _rules.matchLengthTicks / 2U)
    {
      probe.creditsHalf = purse;
    }

    if (match.Tick() == dayFive)
    {
      for (const Lockstep::SystemState& system : match.Systems())
      {
        probe.claimedAtDayFive += system.owner.IsValid() ? 1U : 0U;
      }
    }

    probe.mostTradeLanes = std::max(probe.mostTradeLanes, static_cast<std::uint32_t>(match.TradeLanes().size()));
  }

  for (const Lockstep::PlayerState& player : match.Players())
  {
    probe.creditsEnd += player.credits;
  }

  for (const Lockstep::SystemState& system : match.Systems())
  {
    probe.claimedAtEnd += system.owner.IsValid() ? 1U : 0U;
    if (system.shipyardLevel > 0)
    {
      ++probe.levelsHeld[std::min<std::size_t>(system.shipyardLevel, 3)];
    }
    if (system.miningStationLevel > 0)
    {
      ++probe.levelsHeld[std::min<std::size_t>(system.miningStationLevel, 3)];
    }
  }

  std::vector<std::uint32_t> scores;
  std::uint32_t total = 0;
  for (const Lockstep::PlayerState& player : match.Players())
  {
    scores.push_back(player.score);
    total += player.score;
  }
  std::sort(scores.begin(), scores.end(), std::greater<>());
  probe.leaderScore = scores.front();
  probe.medianScore = scores[scores.size() / 2];
  probe.lastScore = scores.back();
  probe.leaderSharePercent = total == 0 ? 0 : (scores.front() * 100U) / total;

  for (std::size_t index = 0; index < match.GalaxyGraph().Capitals().size(); ++index)
  {
    const Lockstep::SystemId capital = match.GalaxyGraph().Capitals()[index];
    probe.capitalsLost += match.SystemAt(capital).owner.Index() != static_cast<std::int32_t>(index) ? 1U : 0U;
  }

  return probe;
}

void Report(const char* _what, const Lockstep::MatchRules& _rules)
{
  Logger::WriteMessage(std::format("\n==== {} ({} ticks) ====\n", _what, _rules.matchLengthTicks).c_str());
  Logger::WriteMessage("  n  acceptedSeed      cr/4   cr/2   crEnd  lastBuild  starts  L1 L2 L3  d5/end  capsAfterD5  "
                       "sieges  capsLost  lead/med/last  share  battles  lanes  ignored  refused\n");

  for (std::uint64_t index = 0; index < 10; ++index)
  {
    const std::uint64_t seed = 0x4C4F'434B'0000'0001ULL + index * 0x9E37'79B9'7F4A'7C15ULL;
    const Probe probe = PlayOne(seed, _rules);

    Logger::WriteMessage(std::format("{:>3}  {:016X}  {:>5}  {:>5}  {:>6}  {:>9}  {:>6}  {:>2} {:>2} {:>2}  {:>2}/{:<3}  {:>11}  "
                                     "{:>6}  {:>8}  {:>4}/{:>3}/{:<4} {:>4}%  {:>7}  {:>5}  {:>7}  {:>7}\n",
                                     index, probe.acceptedSeed, probe.creditsQuarter, probe.creditsHalf, probe.creditsEnd,
                                     probe.lastBuildStartedAt, probe.buildsStarted, probe.levelsHeld[1], probe.levelsHeld[2],
                                     probe.levelsHeld[3], probe.claimedAtDayFive, probe.claimedAtEnd, probe.capturesAfterDayFive,
                                     probe.siegesBegun, probe.capitalsLost, probe.leaderScore, probe.medianScore, probe.lastScore,
                                     probe.leaderSharePercent, probe.battles, probe.mostTradeLanes, probe.proposalsIgnored,
                                     probe.ordersRefused)
                           .c_str());

    if (index == 0)
    {
      Logger::WriteMessage(std::format("     ({} systems, {} lanes) refusals by reason:\n", probe.systems, 0).c_str());
      for (std::size_t reason = 0; reason < probe.refusalReasons.size(); ++reason)
      {
        if (probe.refusalReasons[reason] > 0)
        {
          Logger::WriteMessage(std::format("       {:>3} x {}\n", probe.refusalReasons[reason],
                                           Lockstep::Describe(static_cast<Lockstep::OrderRejection>(reason)))
                                 .c_str());
        }
      }
    }
  }
}

/// How much force it takes to capture a defended system, and whether a shipyard changes it.
///
/// The bot matches never take ground; `ACaptureIsReachableAgainstADefendedSystem` proves the rules
/// allow it. This is the number between those two facts: what an attacker has to bring. If it is
/// small, the bots are passive and Phase 0 with people will look different; if it is large, the
/// mid-game is static by arithmetic and no amount of human aggression changes that.
void ReportCaptureLadder()
{
  Logger::WriteMessage("\n==== WHAT IT TAKES TO CAPTURE A DEFENDED SYSTEM ====\n");
  Logger::WriteMessage("garrison of 10, attacker arrives and stays, guard window passed\n");
  Logger::WriteMessage("attacker  ratio  no shipyard        with a shipyard\n");

  for (const std::uint32_t attackers : {10U, 15U, 20U, 30U, 40U, 60U, 80U, 100U})
  {
    std::string line = std::format("{:>8}  {:>4}x  ", attackers, attackers / 10U);

    for (const bool shipyard : {false, true})
    {
      Lockstep::MatchRules rules;
      rules.playerCount = 6;
      Lockstep::Match match = Lockstep::Match::Create(rules, 0x4C4F'434B'0000'0001ULL);
      match.SetTick(match.Rules().capitalGuardTicks);

      const Lockstep::SystemId target = match.GalaxyGraph().Capitals()[1];
      if (shipyard)
      {
        match.MutableSystems()[target.AsSize()].shipyardLevel = 1;
      }

      for (Lockstep::MatchFleet& fleet : match.MutableFleets())
      {
        if (fleet.owner == Lockstep::PlayerId{1})
        {
          fleet.ships = 10;
          fleet.at = target;
        }
      }

      Lockstep::MatchFleet raider;
      raider.owner = Lockstep::PlayerId{0};
      raider.ships = attackers;
      raider.at = target;
      (void)match.AddFleet(raider);

      std::int32_t tookUntil = -1;
      for (std::int32_t tick = 0; tick < 12 && tookUntil < 0; ++tick)
      {
        Lockstep::TickLog log;
        match = Lockstep::TickResolver::Resolve(match, {}, log);
        if (match.SystemAt(target).owner == Lockstep::PlayerId{0})
        {
          tookUntil = tick + 1;
        }
      }

      line += tookUntil < 0 ? std::format("{:<19}", "never in 12 ticks") : std::format("took it in {:<8}", tookUntil);
    }

    Logger::WriteMessage((line + "\n").c_str());
  }
}

} // namespace

TEST_CLASS(BalanceProbeTests)
{
public:
  TEST_METHOD(TenMatchesUnderTheAuthoredRules)
  {
    Report("AUTHORED RULES", Lockstep::MatchRules{});
  }

  TEST_METHOD(TenMatchesUnderPhaseZeroRules)
  {
    Report("PHASE 0 PRESET", Lockstep::PhaseZeroRules());
  }

  TEST_METHOD(TheForceRatioACaptureNeeds)
  {
    ReportCaptureLadder();
  }
};

} // namespace GameLogicTests
