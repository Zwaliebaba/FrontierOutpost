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

/// The six policies the plan asks for.
enum class Policy : std::uint8_t
{
  ExpandNear,
  ExpandFar,
  Turtle,
  Raider,
  Diplomat,
  Absentee
};

constexpr std::array<Policy, 6> POLICIES = {Policy::ExpandNear, Policy::ExpandFar, Policy::Turtle,
                                            Policy::Raider,     Policy::Diplomat,  Policy::Absentee};

constexpr std::int32_t ABSENTEE = 5;

/// Systems this player holds and can see right now.
[[nodiscard]] std::vector<Frontier::SystemId> Held(const Frontier::Snapshot& _view)
{
  std::vector<Frontier::SystemId> mine;
  for (const Frontier::SnapshotSystem& system : _view.Systems())
  {
    if (system.live && system.owner == _view.Viewer())
    {
      mine.push_back(system.id);
    }
  }
  return mine;
}

/// Every lane out of `_from` the player knows about, as (destination, cost), lowest id first.
[[nodiscard]] std::vector<std::pair<Frontier::SystemId, std::uint32_t>> Exits(const Frontier::Snapshot& _view, Frontier::SystemId _from)
{
  std::vector<std::pair<Frontier::SystemId, std::uint32_t>> out;
  for (const Frontier::SnapshotLane& lane : _view.Lanes())
  {
    if (lane.a == _from)
    {
      out.emplace_back(lane.b, lane.costTicks);
    }
    else if (lane.b == _from)
    {
      out.emplace_back(lane.a, lane.costTicks);
    }
  }
  return out;
}

[[nodiscard]] const Frontier::SnapshotSystem* Find(const Frontier::Snapshot& _view, Frontier::SystemId _system)
{
  for (const Frontier::SnapshotSystem& system : _view.Systems())
  {
    if (system.id == _system)
    {
      return &system;
    }
  }
  return nullptr;
}

/// Where this fleet should go, by policy. An invalid id means hold.
///
/// A breadth-first walk over the lanes the player KNOWS ABOUT, to the nearest (or furthest) system
/// worth having, returning the first hop toward it. An earlier version of these bots looked only one
/// lane ahead, and every one of them stalled the moment it ran out of adjacent open ground -- six
/// empires sat on two systems each for eighty ticks and never met. That was a harness failure that
/// looked exactly like a rules failure, which is worth remembering: a bot too simple to reach a
/// mechanic will report that the mechanic does not work.
[[nodiscard]] Frontier::SystemId ChooseDestination(Policy _policy, const Frontier::Snapshot& _view, Frontier::SystemId _at)
{
  if (_policy == Policy::Turtle)
  {
    return {};
  }

  // Highest system id the player knows about, so the walk can be indexed rather than searched.
  std::int32_t highest = _at.Index();
  for (const Frontier::SnapshotSystem& system : _view.Systems())
  {
    highest = std::max(highest, system.id.Index());
  }
  const std::size_t count = static_cast<std::size_t>(highest) + 1;

  std::vector<std::vector<Frontier::SystemId>> exits(count);
  for (const Frontier::SnapshotLane& lane : _view.Lanes())
  {
    if (lane.a.AsSize() < count && lane.b.AsSize() < count)
    {
      exits[lane.a.AsSize()].push_back(lane.b);
      exits[lane.b.AsSize()].push_back(lane.a);
    }
  }
  for (std::vector<Frontier::SystemId>& row : exits)
  {
    std::sort(row.begin(), row.end());
  }

  std::vector<std::int32_t> distance(count, -1);
  std::vector<Frontier::SystemId> firstHop(count);
  distance[_at.AsSize()] = 0;

  std::vector<Frontier::SystemId> ring = {_at};
  std::int32_t depth = 0;
  while (!ring.empty())
  {
    std::vector<Frontier::SystemId> next;
    for (const Frontier::SystemId here : ring)
    {
      for (const Frontier::SystemId other : exits[here.AsSize()])
      {
        if (distance[other.AsSize()] >= 0)
        {
          continue;
        }
        distance[other.AsSize()] = depth + 1;
        firstHop[other.AsSize()] = depth == 0 ? other : firstHop[here.AsSize()];
        next.push_back(other);
      }
    }
    ring = next;
    ++depth;
  }

  Frontier::SystemId best;
  std::int32_t bestDistance = 0;
  bool bestIsRival = false;

  for (const Frontier::SnapshotSystem& system : _view.Systems())
  {
    if (system.kind == Frontier::SystemKind::RegionAnchor || system.id == _at)
    {
      continue;
    }
    const std::size_t index = system.id.AsSize();
    if (index >= count || distance[index] < 0 || !firstHop[index].IsValid())
    {
      continue;
    }

    const bool unowned = !system.owner.IsValid();
    const bool rival = system.owner.IsValid() && system.owner != _view.Viewer();
    if (!unowned && !(rival && _policy == Policy::Raider))
    {
      continue;
    }

    // The raider goes for somebody else's ground first and open ground only when there is none.
    // Everyone else takes what is open, near or far by policy.
    const bool preferred = _policy == Policy::Raider && rival;
    bool better = !best.IsValid();
    if (!better && _policy == Policy::Raider && preferred != bestIsRival)
    {
      better = preferred;
    }
    else if (!better && distance[index] != bestDistance)
    {
      better = _policy == Policy::ExpandFar ? distance[index] > bestDistance : distance[index] < bestDistance;
    }
    else if (!better)
    {
      better = system.id < best;
    }

    if (better)
    {
      best = system.id;
      bestDistance = distance[index];
      bestIsRival = preferred;
    }
  }

  return best.IsValid() ? firstHop[best.AsSize()] : Frontier::SystemId{};
}

/// One bot's orders for one tick, from what it can see and nothing else.
[[nodiscard]] Frontier::OrderSet OrdersFor(Policy _policy, const Frontier::Snapshot& _view, const Frontier::MatchRules& _rules)
{
  Frontier::OrderSet orders;
  orders.player = _view.Viewer();

  if (_policy == Policy::Absentee)
  {
    return orders;
  }

  // Answer everything addressed to you. A trade lane is free income and the other two cost nothing,
  // so a bot that declined would be modelling suspicion the design has no mechanic for.
  for (const Frontier::SnapshotProposal& proposal : _view.Proposals())
  {
    if (proposal.to == _view.Viewer())
    {
      orders.answers.push_back(Frontier::AnswerOrder{.proposal = proposal.id, .answer = Frontier::Answer::Accept});
    }
  }

  // Move whatever is parked.
  for (const Frontier::SnapshotFleet& fleet : _view.Fleets())
  {
    if (fleet.owner != _view.Viewer() || fleet.ticksRemaining > 0 || !fleet.at.IsValid())
    {
      continue;
    }

    const Frontier::SystemId destination = ChooseDestination(_policy, _view, fleet.at);
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = fleet.id, .destination = destination.IsValid() ? destination : fleet.at});
  }

  // Build, cheapest useful thing first, on the lowest-numbered system that lacks one. The turtle
  // builds shipyards because it never moves; everybody else builds income.
  const std::vector<Frontier::SystemId> mine = Held(_view);
  const bool yardFirst = _policy == Policy::Turtle;

  for (const Frontier::SystemId system : mine)
  {
    const Frontier::SnapshotSystem* state = Find(_view, system);
    if (state == nullptr)
    {
      continue;
    }

    if (yardFirst && !state->hasShipyard)
    {
      orders.builds.push_back(Frontier::BuildOrder{.system = system, .kind = Frontier::BuildKind::Shipyard});
      break;
    }
    if (!yardFirst && !state->hasMiningStation)
    {
      orders.builds.push_back(Frontier::BuildOrder{.system = system, .kind = Frontier::BuildKind::MiningStation});
      break;
    }
  }

  // The diplomat offers a lane wherever its territory touches somebody else's and nothing is open
  // or pending there. One offer a tick, so it does not flood the board.
  if (_policy == Policy::Diplomat)
  {
    for (const Frontier::SnapshotLane& lane : _view.Lanes())
    {
      if (lane.tradeLane)
      {
        continue;
      }

      const Frontier::SnapshotSystem* first = Find(_view, lane.a);
      const Frontier::SnapshotSystem* second = Find(_view, lane.b);
      if (first == nullptr || second == nullptr || !first->live || !second->live)
      {
        continue;
      }

      const bool mineFirst = first->owner == _view.Viewer();
      const bool mineSecond = second->owner == _view.Viewer();
      if (mineFirst == mineSecond)
      {
        continue;
      }

      const Frontier::SnapshotSystem* theirs = mineFirst ? second : first;
      if (!theirs->owner.IsValid())
      {
        continue;
      }

      const bool pending = std::any_of(_view.Proposals().begin(), _view.Proposals().end(),
                                       [&lane](const Frontier::SnapshotProposal& _open) { return _open.lane == lane.id; });
      if (pending)
      {
        continue;
      }

      if (_view.Standings()[_view.Viewer().AsSize()].score > 0)
      {
        orders.proposals.push_back(Frontier::ProposalOrder{.to = theirs->owner, .kind = Frontier::ProposalKind::OpenLane, .lane = lane.id});
      }
      break;
    }
  }

  (void)_rules;
  return orders;
}

/// Everything that must be true of a match at the end of every tick.
///
/// Returned as a sentence rather than asserted here, so the failure names the tick it happened on
/// -- "invariant violated" on tick 61 of 84 is a bug report nobody can act on.
[[nodiscard]] std::string Violation(const Frontier::Match& _match)
{
  if (!_match.IsConsistent())
  {
    return "IsConsistent failed";
  }

  for (std::size_t index = 0; index < _match.Fleets().size(); ++index)
  {
    const Frontier::MatchFleet& fleet = _match.Fleets()[index];
    if (fleet.destroyed)
    {
      continue;
    }

    if (fleet.InTransit())
    {
      // A fleet under way must be on a lane that exists. Anything else is a fleet in open space,
      // which this game does not have -- "not a coordinate map".
      bool onALane = false;
      for (const Frontier::LaneId lane : _match.GalaxyGraph().LanesAt(fleet.movingFrom))
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

  for (const Frontier::OpenProposal& proposal : _match.Proposals())
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

  for (const Frontier::ActiveTradeLane& lane : _match.TradeLanes())
  {
    const Frontier::GalaxyLane& edge = _match.GalaxyGraph().LaneAt(lane.lane);
    const Frontier::PlayerId first = _match.SystemAt(edge.a).owner;
    const Frontier::PlayerId second = _match.SystemAt(edge.b).owner;
    if (!((first == lane.a && second == lane.b) || (first == lane.b && second == lane.a)))
    {
      return "a trade lane outlived its endpoints";
    }
  }

  return {};
}

struct Played
{
  Frontier::Match match;
  std::uint32_t ticks = 0;
  /// Highest count seen at any point, since lanes open and close.
  std::size_t mostTradeLanes = 0;
  bool absenteeWentIntoCustody = false;
  std::uint32_t proposalsMade = 0;
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
  Frontier::MatchRules rules;
  rules.playerCount = 6;

  Played played{.match = Frontier::Match::Create(rules, SEED)};

  // Everybody but the absentee, every tick. Presence is the server's to report and the harness is
  // standing in for one.
  std::vector<Frontier::PlayerId> present;
  for (std::int32_t player = 0; player < 6; ++player)
  {
    if (player != ABSENTEE)
    {
      present.emplace_back(player);
    }
  }

  while (!played.match.IsFinished())
  {
    std::vector<Frontier::OrderSet> orders;
    orders.reserve(POLICIES.size());
    for (std::size_t index = 0; index < POLICIES.size(); ++index)
    {
      const Frontier::PlayerId player{static_cast<std::int32_t>(index)};
      const Frontier::Snapshot view = Frontier::Snapshot::For(played.match, player);
      orders.push_back(OrdersFor(POLICIES[index], view, played.match.Rules()));

      if (POLICIES[index] == Policy::Diplomat)
      {
        played.proposalsMade += static_cast<std::uint32_t>(orders.back().proposals.size());
        for (const Frontier::SnapshotLane& lane : view.Lanes())
        {
          const Frontier::SnapshotSystem* first = Find(view, lane.a);
          const Frontier::SnapshotSystem* second = Find(view, lane.b);
          if (first != nullptr && second != nullptr && first->live && second->live &&
              (first->owner == player) != (second->owner == player) && (first->owner.IsValid() && second->owner.IsValid()))
          {
            ++played.diplomatTouchedARival;
            break;
          }
        }
      }
    }

    Frontier::TickLog log;
    const auto startedAt = std::chrono::steady_clock::now();
    played.match = Frontier::TickResolver::Resolve(
      played.match, Frontier::TickInput{.orders = orders, .present = present, .presenceUnknown = false}, log);
    played.totalResolveMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();

    ++played.ticks;
    played.mostTradeLanes = std::max(played.mostTradeLanes, played.match.TradeLanes().size());

    if (!played.absenteeWentIntoCustody && played.match.PlayerAt(Frontier::PlayerId{ABSENTEE}).status == Frontier::PlayerStatus::Custodian)
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

  // The absentee never logs in, so custody should arrive on the tick the rule says and not later.
  TEST_METHOD(TheAbsenteeGoesIntoCustodyOnSchedule)
  {
    const Played played = PlayAMatch();

    Assert::IsTrue(played.absenteeWentIntoCustody, L"three weeks away and still active would be a broken rule");
    Assert::AreEqual(played.match.Rules().custodianAbsenceTicks, played.absenteeCustodyTick, L"on the tick the rule names, not eventually");
    Assert::IsTrue(played.match.PlayerAt(Frontier::PlayerId{ABSENTEE}).forfeitedScore, L"and inside the first week, so they score nothing");
    Assert::AreEqual(0U, played.match.PlayerAt(Frontier::PlayerId{ABSENTEE}).score);
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
    Assert::IsTrue(played.match.PlayerAt(Frontier::PlayerId{DIPLOMAT}).credits > 0U, L"and the diplomat has income to show for it");
  }

  // Something has to be happening. A match where nobody expands is a match where the harness is
  // driving nothing and every assertion above passes vacuously.
  TEST_METHOD(TheGalaxyIsFoughtOverRatherThanIgnored)
  {
    const Played played = PlayAMatch();

    std::uint32_t claimed = 0;
    for (const Frontier::SystemState& system : played.match.Systems())
    {
      if (system.owner.IsValid())
      {
        ++claimed;
      }
    }

    Assert::IsTrue(claimed > 6U, L"somebody expanded past their capital");

    std::uint32_t withBuildings = 0;
    for (const Frontier::SystemState& system : played.match.Systems())
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
