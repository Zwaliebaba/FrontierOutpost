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

#include "SnapshotView.h"

#include "BotPolicy.h"
#include "ByteWriter.h"
#include "MatchSimulation.h"

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
