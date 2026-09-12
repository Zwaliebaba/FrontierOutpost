// SignalTests.cpp -- what a player can say, and what it turns into on the wire.
//
// ADR-039 gave the client the four order kinds it could not express. THIS FILE IS WHY THE PROJECT
// EXISTS: the signal path was first proved by a throwaway harness compiled outside the repository,
// because `Lockstep` is an executable and nothing could link it. A verification that has to be
// rebuilt by hand every time is a verification nobody runs twice.
//
// These drive a REAL MATCH rather than a hand-built `MatchState`. `ComposeSignals` reads a snapshot
// and asks questions about it -- does this lane have one of my systems at one end and a rival's at
// the other, has this empire actually been met -- and a fixture built to answer them would be a
// fixture built from the same assumptions the code makes. A galaxy the generator produced and
// fourteen ticks of bots playing it is a board neither of them arranged.

#include "pch.h"
#include "CppUnitTest.h"

#include "MainPage.h"
#include "SnapshotView.h"

#include "BotPolicy.h"
#include "ByteWriter.h"
#include "MatchSimulation.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

namespace
{

constexpr std::uint64_t SIGNAL_SEED = 0x5349'474E'414C'5321ULL;

/// A match with bots in every seat but the first, run far enough that borders touch.
///
/// Seat zero is left to the tests: a human seat nobody plays, which is what the client under test
/// is standing in for.
///
/// Held by pointer because `Neuron::Simulation` is neither copyable nor movable -- it is the thing
/// a `Session` owns, and nothing else may take a copy of it.
[[nodiscard]] std::unique_ptr<Lockstep::MatchSimulation> PlayedMatch(std::int32_t _ticks)
{
  Lockstep::MatchRules rules;
  rules.playerCount = 6;

  std::vector<std::optional<Lockstep::BotPolicy>> bots(6, std::optional<Lockstep::BotPolicy>{Lockstep::BotPolicy::ExpandNear});
  bots[0].reset();

  auto simulation = std::make_unique<Lockstep::MatchSimulation>(rules, SIGNAL_SEED, bots);
  for (std::int32_t tick = 0; tick < _ticks; ++tick)
  {
    simulation->Resolve();
  }
  return simulation;
}

/// What seat zero's client would be looking at.
[[nodiscard]] Lockstep::MatchState ViewOfSeatZero(const Lockstep::MatchSimulation& _simulation)
{
  const Lockstep::PlayerId seat{0};
  return Lockstep::ViewOf(Lockstep::Snapshot::For(_simulation.State(), seat), Lockstep::Snapshot::DigestFor(_simulation.LastTick(), seat),
                          0);
}

[[nodiscard]] std::vector<std::uint8_t> Encoded(const Lockstep::OrderSet& _orders)
{
  Neuron::ByteWriter writer;
  _orders.Write(writer);
  return writer.Bytes();
}

/// The first row of a kind, or -1. Tests ask for a kind rather than an index because the list is
/// composed from a live board and its length is not something a test should be pinning.
[[nodiscard]] std::int32_t FirstOfKind(const Lockstep::MatchState& _state, Lockstep::SignalKind _kind)
{
  for (std::size_t index = 0; index < _state.orders.signals.size(); ++index)
  {
    if (_state.orders.signals[index].kind == _kind)
    {
      return static_cast<std::int32_t>(index);
    }
  }
  return -1;
}

/// Submits an order set for seat zero and resolves. Returns what the client sees afterwards.
[[nodiscard]] Lockstep::MatchState Play(Lockstep::MatchSimulation& _simulation, const Lockstep::OrderSet& _orders)
{
  _simulation.Submit(0, Encoded(_orders));
  _simulation.MarkPresent(0);
  _simulation.Resolve();
  return ViewOfSeatZero(_simulation);
}

} // namespace

// Where a fleet under way is drawn, and whether the page knows it has to keep drawing it (ADR-055).
// The dots themselves are a screenshot; the fraction they run along and the request for a frame
// are decisions, and these are them.
TEST_CLASS(FleetRouteTests)
{
public:
  /// A match in which SEAT ZERO moves too, which the shared helper deliberately does not do.
  ///
  /// `ViewOf` carries a fleet only when both ends of its lane are systems the viewer can see, so a
  /// rival crossing the dark is public and undrawable. The fleet this test needs to find is the
  /// viewer's own, and the viewer's own fleet moves only if something orders it.
  [[nodiscard]] static std::unique_ptr<Lockstep::MatchSimulation> MatchWhereEverybodyMoves(std::int32_t _ticks)
  {
    Lockstep::MatchRules rules;
    rules.playerCount = 6;

    const std::vector<std::optional<Lockstep::BotPolicy>> bots(6, std::optional<Lockstep::BotPolicy>{Lockstep::BotPolicy::ExpandNear});
    auto simulation = std::make_unique<Lockstep::MatchSimulation>(rules, SIGNAL_SEED, bots);
    for (std::int32_t tick = 0; tick < _ticks; ++tick)
    {
      simulation->Resolve();
    }
    return simulation;
  }

  TEST_METHOD(AFleetInTransitStandsWhereItsRemainingTicksSay)
  {
    // Played far enough that a fleet is on a lane of more than one tick. A one-tick lane is crossed
    // inside the tick and is never seen in transit, which is why the fixed midpoint survived as
    // long as it did: on a two-tick lane it is the right answer.
    bool sawOne = false;
    for (std::int32_t ticks = 1; ticks <= 14 && !sawOne; ++ticks)
    {
      const auto simulation = MatchWhereEverybodyMoves(ticks);
      const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

      for (const Lockstep::Fleet& fleet : state.fleets)
      {
        if (fleet.order != Lockstep::FleetStance::Move || fleet.from == fleet.to)
        {
          continue;
        }

        // The lane it is on, and the ticks it has left, both read off the same state the map draws
        // from. `progress` has to be what those two say, and nothing else.
        std::uint32_t cost = 0;
        for (const Lockstep::Lane& lane : state.graph.lanes)
        {
          if ((lane.a == fleet.from && lane.b == fleet.to) || (lane.a == fleet.to && lane.b == fleet.from))
          {
            cost = lane.cost;
            break;
          }
        }
        if (cost == 0)
        {
          continue;
        }

        const std::uint32_t left = fleet.eta - state.match.tick;
        const float expected = static_cast<float>(cost - left) / static_cast<float>(cost);
        Assert::AreEqual(expected, fleet.progress, 0.001F, L"a fleet is drawn somewhere other than where its remaining ticks put it");
        Assert::IsTrue(fleet.progress > 0.0F && fleet.progress < 1.0F, L"and it is on the lane rather than at either end of it");
        sawOne = true;
      }
    }
    Assert::IsTrue(sawOne, L"fourteen ticks of six bots put nothing in transit, so this test proved nothing");
  }

  TEST_METHOD(NoTwoEventsOfferTheSameBuild)
  {
    // `EventKind::Economy` is the collapsed kind: a claim, a lane, a refusal and a production line
    // all wear it. Testing it put the same `MINING STATION DOTHAN` button on every economy event in
    // the tick, and a digest of three carried three copies of one order (ADR-057).
    for (std::int32_t ticks = 1; ticks <= 14; ++ticks)
    {
      const auto simulation = MatchWhereEverybodyMoves(ticks);
      const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

      std::vector<std::int32_t> offered;
      for (const Lockstep::DigestEvent& event : state.digest)
      {
        for (const Lockstep::EventAction& action : event.actions)
        {
          if (action.kind != Lockstep::EventActionKind::QueueBuild)
          {
            continue;
          }
          Assert::IsTrue(std::ranges::find(offered, action.target) == offered.end(), L"two events offered the same build row");
          Assert::IsTrue(action.target >= 0 && action.target < static_cast<std::int32_t>(state.orders.builds.size()),
                         L"a build button names a row that does not exist");
          offered.push_back(action.target);
        }
      }
    }
  }

  TEST_METHOD(AMapButtonNamesASystemAndNotADigestEntry)
  {
    // The bug this pins is an index meaning two things. MAP carries the system it points at, and it
    // was routed to an action that read the DIGEST with it -- so the bounds check swallowed every
    // tap whose system sat past the end of a short digest, and the button did nothing (ADR-057).
    for (std::int32_t ticks = 1; ticks <= 14; ++ticks)
    {
      const auto simulation = MatchWhereEverybodyMoves(ticks);
      const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

      for (const Lockstep::DigestEvent& event : state.digest)
      {
        for (const Lockstep::EventAction& action : event.actions)
        {
          if (action.kind == Lockstep::EventActionKind::Focus)
          {
            Assert::IsTrue(action.target >= 0 && action.target < static_cast<std::int32_t>(state.graph.systems.size()),
                           L"a MAP button names something that is not a system on this map");
          }
        }
      }
    }
  }

  TEST_METHOD(TheLeaderLineNamesSomebodyElseOrIsNotThere)
  {
    // ADR-056. "Public score, the leader is always visible" is about knowing who is ahead of you,
    // so the one player it never has to name is the viewer -- the chip beside it already says
    // `1ST / 6` and the score beside that is the same number.
    const auto simulation = PlayedMatch(0);
    const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    Assert::AreEqual(1U, state.player.placement, L"at tick zero the tiebreak puts seat zero first");
    Assert::AreEqual(std::string{"YOU"}, state.player.leader.name, L"and the leader IS the viewer, which is what the bar must not say");
  }

  TEST_METHOD(ThePageAsksForFramesOnlyWhileSomethingIsUnderWay)
  {
    // The idle throttle is what this protects: a board with nothing moving must not ask the loop
    // to redraw it sixty times a second.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState still = ViewOfSeatZero(*simulation);
    for (Lockstep::Fleet& fleet : still.fleets)
    {
      fleet.order = Lockstep::FleetStance::Hold;
    }

    Lockstep::MainPage idle;
    idle.Create(std::move(still));
    Assert::IsFalse(idle.Animating(), L"a still board asked for frames it does not need");

    Lockstep::MatchState moving = ViewOfSeatZero(*simulation);
    Assert::IsFalse(moving.fleets.empty(), L"the opening board has a fleet");
    moving.fleets.front().order = Lockstep::FleetStance::Move;
    moving.fleets.front().from = 0;
    moving.fleets.front().to = 1;

    Lockstep::MainPage animated;
    animated.Create(std::move(moving));
    Assert::IsTrue(animated.Animating(), L"a fleet under way did not ask for a frame, so its route would not move");
  }
};

TEST_CLASS(SignalTests)
{
public:
  TEST_METHOD(APlayerWithNeighborsHasSomethingToSay)
  {
    const auto simulation = PlayedMatch(14);
    const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    Assert::IsFalse(state.orders.signals.empty(), L"fourteen ticks in and there is nothing this empire can say to anybody");
    Assert::IsTrue(FirstOfKind(state, Lockstep::SignalKind::OpenLane) >= 0, L"a border with a rival offers no lane");
    Assert::IsTrue(FirstOfKind(state, Lockstep::SignalKind::ShareScouting) >= 0, L"an empire that has been met cannot be offered scouting");
    Assert::IsTrue(FirstOfKind(state, Lockstep::SignalKind::HoldFire) >= 0, L"an empire that has been met cannot be offered a hold");
  }

  TEST_METHOD(ConcedingIsAlwaysOfferedAndIsAlwaysLast)
  {
    // Its position is part of the design: a row that moves between ticks is a row somebody
    // double-taps by accident, and this is the one row that cannot be taken back afterwards.
    for (const std::int32_t ticks : {0, 3, 14})
    {
      const auto simulation = PlayedMatch(ticks);
      const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

      Assert::IsFalse(state.orders.signals.empty(), L"conceding was not offered at all");
      Assert::IsTrue(state.orders.signals.back().kind == Lockstep::SignalKind::Concede, L"conceding is not the last row");
    }
  }

  TEST_METHOD(NothingIsOfferedAboutAnEmpireNobodyHasMet)
  {
    // At tick zero every empire is on its own capital and nobody has seen anybody. An offer to
    // share maps here would be an offer to share a map of somewhere neither of them has been.
    const auto simulation = PlayedMatch(0);
    const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    Assert::IsTrue(FirstOfKind(state, Lockstep::SignalKind::ShareScouting) < 0, L"scouting was offered to somebody nobody has met");
    Assert::IsTrue(FirstOfKind(state, Lockstep::SignalKind::OpenLane) < 0, L"a lane was offered across a border that does not exist yet");
  }

  TEST_METHOD(AQueuedOfferBecomesAProposalOrder)
  {
    const auto simulation = PlayedMatch(14);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    state.orders.queuedSignals.push_back(FirstOfKind(state, Lockstep::SignalKind::OpenLane));
    state.orders.queuedSignals.push_back(FirstOfKind(state, Lockstep::SignalKind::ShareScouting));

    const Lockstep::OrderSet orders = Lockstep::OrdersOf(state);
    Assert::AreEqual(std::size_t{2}, orders.proposals.size(), L"two queued offers did not become two proposal orders");
    Assert::IsTrue(orders.proposals[0].kind == Lockstep::ProposalKind::OpenLane);
    Assert::IsTrue(orders.proposals[0].lane.IsValid(), L"a lane offer with no lane on it");
    Assert::IsTrue(orders.proposals[1].kind == Lockstep::ProposalKind::ShareScouting);
    Assert::IsFalse(orders.concede, L"something conceded that nobody asked to concede");
  }

  TEST_METHOD(AnOfferTheServerAcceptedComesBackAsSomethingToWithdraw)
  {
    // The round trip, and the reason `Snapshot` reports a proposal to BOTH parties: an offer this
    // player made is not in the PROPOSALS rail, because it is not awaiting their answer -- it is
    // here, as the one thing they can still do about it.
    const auto simulation = PlayedMatch(14);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsTrue(FirstOfKind(state, Lockstep::SignalKind::Withdraw) < 0, L"there was something to withdraw before anything was sent");

    state.orders.queuedSignals.push_back(FirstOfKind(state, Lockstep::SignalKind::OpenLane));
    const Lockstep::MatchState after = Play(*simulation, Lockstep::OrdersOf(state));

    Assert::AreEqual(0U, simulation->RejectedSubmissions(), L"the server refused the order set as malformed");
    Assert::IsTrue(FirstOfKind(after, Lockstep::SignalKind::Withdraw) >= 0, L"an offer that was sent cannot be taken back");
  }

  TEST_METHOD(AWithdrawRowBecomesAWithdrawOrderTheServerTakes)
  {
    const auto simulation = PlayedMatch(14);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.orders.queuedSignals.push_back(FirstOfKind(state, Lockstep::SignalKind::OpenLane));

    Lockstep::MatchState after = Play(*simulation, Lockstep::OrdersOf(state));
    const std::int32_t withdraw = FirstOfKind(after, Lockstep::SignalKind::Withdraw);
    Assert::IsTrue(withdraw >= 0);

    after.orders.queuedSignals.push_back(withdraw);
    const Lockstep::OrderSet taking = Lockstep::OrdersOf(after);
    Assert::AreEqual(std::size_t{1}, taking.withdrawals.size(), L"a withdraw row did not become a withdraw order");
    Assert::IsTrue(taking.withdrawals[0].proposal.IsValid(), L"a withdraw naming no proposal");

    const Lockstep::MatchState later = Play(*simulation, taking);
    Assert::AreEqual(0U, simulation->RejectedSubmissions(), L"the server refused the withdraw");
    Assert::IsTrue(FirstOfKind(later, Lockstep::SignalKind::Withdraw) < 0, L"the offer is still open after being withdrawn");
  }

  TEST_METHOD(ConcedingReachesTheServerAndEndsTheEmpire)
  {
    const auto simulation = PlayedMatch(14);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    state.orders.queuedSignals.push_back(FirstOfKind(state, Lockstep::SignalKind::Concede));
    const Lockstep::OrderSet orders = Lockstep::OrdersOf(state);
    Assert::IsTrue(orders.concede, L"the concede row did not become a concede order");

    (void)Play(*simulation, orders);
    Assert::AreEqual(0U, simulation->RejectedSubmissions(), L"the server refused the concede");

    bool custodian = false;
    for (const std::string& line : simulation->TakeEvents())
    {
      custodian = custodian || line.find("custodian") != std::string::npos;
    }
    Assert::IsTrue(custodian, L"conceding did not put the empire into custody");
  }

  TEST_METHOD(AnUnqueuedListSendsNothing)
  {
    // The negative that makes every test above mean something: composing a row is not sending it.
    const auto simulation = PlayedMatch(14);
    const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    const Lockstep::OrderSet orders = Lockstep::OrdersOf(state);
    Assert::IsTrue(orders.proposals.empty(), L"an offer went out that nobody queued");
    Assert::IsTrue(orders.withdrawals.empty());
    Assert::IsTrue(orders.cancellations.empty());
    Assert::IsFalse(orders.concede, L"the empire conceded itself");
  }

  TEST_METHOD(AnOutOfRangeQueueEntryIsIgnoredRatherThanRead)
  {
    // `queuedSignals` holds indices, and the screen rebuilds the list every time a state arrives.
    // An index left over from a longer list must not be followed into the vector.
    const auto simulation = PlayedMatch(14);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    state.orders.queuedSignals.push_back(-1);
    state.orders.queuedSignals.push_back(static_cast<std::int32_t>(state.orders.signals.size()) + 40);

    const Lockstep::OrderSet orders = Lockstep::OrdersOf(state);
    Assert::IsTrue(orders.proposals.empty());
    Assert::IsFalse(orders.concede);
  }

  TEST_METHOD(TheListIsStableBetweenTwoViewsOfTheSameBoard)
  {
    // The rows are tapped. If the same board produced them in a different order from one tick to
    // the next, a player's finger would land on whatever had moved into that slot.
    const auto simulation = PlayedMatch(14);
    const Lockstep::MatchState first = ViewOfSeatZero(*simulation);
    const Lockstep::MatchState second = ViewOfSeatZero(*simulation);

    Assert::AreEqual(first.orders.signals.size(), second.orders.signals.size());
    for (std::size_t index = 0; index < first.orders.signals.size(); ++index)
    {
      Assert::IsTrue(first.orders.signals[index].kind == second.orders.signals[index].kind, L"a row changed kind");
      Assert::AreEqual(first.orders.signals[index].title, second.orders.signals[index].title, L"a row changed title");
    }
  }

  TEST_METHOD(EveryRowCarriesWhatItsOrderNeeds)
  {
    // A row has to be able to BECOME an order. This is the check that a composer change cannot
    // quietly produce a row that turns into an order naming nobody.
    const auto simulation = PlayedMatch(14);
    const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    for (const Lockstep::SignalRow& signal : state.orders.signals)
    {
      const std::wstring where = L"a row does not carry what its order needs";
      switch (signal.kind)
      {
      case Lockstep::SignalKind::OpenLane:
        Assert::IsTrue(signal.to >= 0 && signal.lane >= 0, where.c_str());
        break;
      case Lockstep::SignalKind::ShareScouting:
        Assert::IsTrue(signal.to >= 0, where.c_str());
        break;
      case Lockstep::SignalKind::HoldFire:
        Assert::IsTrue(signal.to >= 0 && signal.ticks > 0, where.c_str());
        break;
      case Lockstep::SignalKind::Withdraw:
        Assert::IsTrue(signal.proposal >= 0, where.c_str());
        break;
      case Lockstep::SignalKind::CancelLane:
        Assert::IsTrue(signal.lane >= 0, where.c_str());
        break;
      case Lockstep::SignalKind::Concede:
      default:
        break;
      }
      Assert::IsFalse(signal.title.empty(), L"a row with nothing written on it");
    }
  }
};

} // namespace LockstepTests
