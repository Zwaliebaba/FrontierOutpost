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
#include "TickResolver.h"

#include <algorithm>
#include <format>
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

/// Seat zero, looking at offers from `_from` -- one per entry, so a list of two rivals puts two
/// offers on the table and a list of one rival twice puts two from the same rival.
///
/// `HoldForTicks` is the offer used because it is the one with no board precondition: a lane offer
/// has to join two systems the two players hold, which would make this helper a map-building
/// exercise about something these tests are not about.
[[nodiscard]] Lockstep::MatchState SeatZeroLookingAtOffersFrom(std::initializer_list<std::int32_t> _from)
{
  Lockstep::MatchRules rules;
  rules.playerCount = 6;
  Lockstep::Match match = Lockstep::Match::Create(rules, SIGNAL_SEED);

  std::vector<Lockstep::OrderSet> sets;
  for (const std::int32_t sender : _from)
  {
    const auto existing = std::find_if(sets.begin(), sets.end(),
                                       [sender](const Lockstep::OrderSet& _set) { return _set.player == Lockstep::PlayerId{sender}; });
    Lockstep::OrderSet& set = existing != sets.end() ? *existing : sets.emplace_back();
    set.player = Lockstep::PlayerId{sender};
    set.proposals.push_back(Lockstep::ProposalOrder{.to = Lockstep::PlayerId{0}, .kind = Lockstep::ProposalKind::HoldForTicks, .ticks = 2});
  }

  Lockstep::TickLog log;
  match = Lockstep::TickResolver::Resolve(match, {.orders = sets}, log);

  const Lockstep::PlayerId seat{0};
  return Lockstep::ViewOf(Lockstep::Snapshot::For(match, seat), Lockstep::Snapshot::DigestFor(log, seat), 0);
}

/// The action of a kind on the card about `_proposal`, or nothing.
[[nodiscard]] std::optional<Lockstep::EventAction> ActionOn(const Lockstep::MatchState& _state, std::int32_t _proposal,
                                                            Lockstep::EventActionKind _kind)
{
  for (const Lockstep::DigestEvent& event : _state.digest)
  {
    for (const Lockstep::EventAction& action : event.actions)
    {
      if (action.kind == _kind && action.target == _proposal)
      {
        return action;
      }
    }
  }
  return std::nullopt;
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

  // The row states the cost rather than implying it (ADR-067). A player reads this line between
  // arming the row and confirming it (ADR-064), and it is the only place the client says that the
  // score goes with the empire.
  TEST_METHOD(TheConcedeRowSaysWhatItCosts)
  {
    const auto simulation = PlayedMatch(14);
    const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    const std::string& detail = state.orders.signals.back().detail;
    Assert::IsTrue(detail.find("score") != std::string::npos, L"the concede row does not mention the score it forfeits");
    Assert::IsTrue(detail.find("permanently") != std::string::npos, L"the concede row does not say it is permanent");

    // It shares its row with the armed state's right-hand text, so it has to fit beside it: the
    // sheet is 620 - 2*12 wide, less 10 of padding each side, less TAP AGAIN TO CONFIRM at 8px a
    // character. Measured here rather than eyeballed, because an overrun draws one string over
    // another rather than failing.
    constexpr std::size_t ROOM_FOR_THE_DETAIL = (620 - 2 * 12 - 2 * 10 - 20 * 8) / 8;
    Assert::IsTrue(detail.size() <= ROOM_FOR_THE_DETAIL, L"the concede row's second line runs into TAP AGAIN TO CONFIRM");
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

// An offer is answered on the card that reports it, so the card has to know WHICH offer it is
// about (ADR-068). Before the proposal reached the digest entry this was matched by comparing a
// proposal id against `other` -- a player id -- behind a `size() == 1` fallback that made it right
// exactly while one offer was open, which is every test and both rehearsals to date.
TEST_CLASS(ProposalCardTests)
{
public:
  // The four build kinds reach the screen as cards (ADR-069). Driven through a real order rather
  // than a hand-built digest, because what is under test is the whole path: the resolver writes the
  // entry, `ColorOf` picks its dot, and the card carries the ETA the player is committing to.
  TEST_METHOD(OrderingALevelPutsItsETAOnTheDigestAndTheRail)
  {
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    Lockstep::Match match = Lockstep::Match::Create(rules, SIGNAL_SEED);

    const Lockstep::SystemId capital = match.GalaxyGraph().Capitals()[0];
    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.builds.push_back(Lockstep::BuildOrder{.system = capital, .kind = Lockstep::BuildKind::MiningStation});
    const std::vector<Lockstep::OrderSet> sets = {orders};

    Lockstep::TickLog log;
    match = Lockstep::TickResolver::Resolve(match, {.orders = sets}, log);
    const std::uint32_t lands = match.SystemAt(capital).construction.completesAt;
    Assert::IsTrue(lands > 0, L"the order started nothing");

    const Lockstep::PlayerId seat{0};
    const Lockstep::MatchState state = Lockstep::ViewOf(Lockstep::Snapshot::For(match, seat), Lockstep::Snapshot::DigestFor(log, seat), 0);

    // The card. It is economy-coloured -- a commitment, not a loss -- and says when it lands.
    const std::string when = std::format("T{}", lands);
    const bool onACard = std::any_of(state.digest.begin(), state.digest.end(), [&when](const Lockstep::DigestEvent& _event)
                                     { return _event.detail.find(when) != std::string::npos; });
    Assert::IsTrue(onACard, L"nothing in the digest says when the building lands");

    // And the rail lists it as in flight, with the same tick, linking to the system it is on.
    const auto rising =
      std::find_if(state.orders.builds.begin(), state.orders.builds.end(), [](const Lockstep::BuildRow& _row) { return _row.rising; });
    Assert::IsTrue(rising != state.orders.builds.end(), L"the rail does not list what is rising");
    Assert::AreEqual(lands, rising->completesAt, L"the rail and the board disagree about when it lands");
    Assert::IsFalse(rising->available, L"a rising row is offered as something to queue");
    Assert::AreEqual(0U,
                     state.orders.availableBuilds -
                       static_cast<std::uint32_t>(std::count_if(state.orders.builds.begin(), state.orders.builds.end(),
                                                                [](const Lockstep::BuildRow& _row) { return !_row.rising; })),
                     L"the AVAIL count includes a row that cannot be started");
  }

  TEST_METHOD(TwoOffersEachCarryTheirOwnButtons)
  {
    const Lockstep::MatchState state = SeatZeroLookingAtOffersFrom({1, 2});
    Assert::AreEqual(std::size_t{2}, state.proposals.size(), L"two rivals did not put two offers on the table");

    for (std::int32_t index = 0; index < static_cast<std::int32_t>(state.proposals.size()); ++index)
    {
      const std::wstring which = L"offer " + std::to_wstring(index);
      Assert::IsTrue(ActionOn(state, index, Lockstep::EventActionKind::AcceptProposal).has_value(),
                     (which + L" has no ACCEPT aimed at it").c_str());
      Assert::IsTrue(ActionOn(state, index, Lockstep::EventActionKind::DeclineProposal).has_value(),
                     (which + L" has no DECLINE aimed at it").c_str());
    }
  }

  TEST_METHOD(TwoOffersFromOneRivalAreStillToldApart)
  {
    // The case sender-matching could never have got right: both cards name the same player.
    const Lockstep::MatchState state = SeatZeroLookingAtOffersFrom({1, 1});
    Assert::AreEqual(std::size_t{2}, state.proposals.size());
    Assert::IsTrue(state.proposals[0].from == state.proposals[1].from, L"this test needs both offers from one rival");

    Assert::IsTrue(ActionOn(state, 0, Lockstep::EventActionKind::AcceptProposal).has_value());
    Assert::IsTrue(ActionOn(state, 1, Lockstep::EventActionKind::AcceptProposal).has_value());
  }

  TEST_METHOD(EveryAnswerReachesTheSameLock)
  {
    // `OrderSet::answers` is a list and the resolver applies all of it; the client used to hold one
    // answer per tick, so the second offer waited a lock it could expire in.
    Lockstep::MatchState state = SeatZeroLookingAtOffersFrom({1, 2});
    state.orders.answers.push_back(Lockstep::ProposalAnswer{.proposal = 0, .accepted = true});
    state.orders.answers.push_back(Lockstep::ProposalAnswer{.proposal = 1, .accepted = false});

    const Lockstep::OrderSet orders = Lockstep::OrdersOf(state);
    Assert::AreEqual(std::size_t{2}, orders.answers.size(), L"both answers did not reach one order set");
    Assert::IsTrue(orders.answers[0].proposal == Lockstep::ProposalId{state.proposals[0].id}, L"the first answer names the wrong offer");
    Assert::IsTrue(orders.answers[0].answer == Lockstep::Answer::Accept);
    Assert::IsTrue(orders.answers[1].proposal == Lockstep::ProposalId{state.proposals[1].id}, L"the second answer names the wrong offer");
    Assert::IsTrue(orders.answers[1].answer == Lockstep::Answer::Decline);
  }

  TEST_METHOD(AWithdrawnOffersCardCarriesNoButtons)
  {
    // A digest entry about an offer that is no longer open -- withdrawn here, voided or already
    // resolved elsewhere -- names an id nothing open matches, and so draws no buttons. Under the
    // fallback this was the dangerous case: one entry, one unrelated offer, buttons on both.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    Lockstep::Match match = Lockstep::Match::Create(rules, SIGNAL_SEED);

    Lockstep::OrderSet offer;
    offer.player = Lockstep::PlayerId{1};
    offer.proposals.push_back(
      Lockstep::ProposalOrder{.to = Lockstep::PlayerId{0}, .kind = Lockstep::ProposalKind::HoldForTicks, .ticks = 2});
    const std::vector<Lockstep::OrderSet> offered = {offer};

    Lockstep::TickLog opened;
    match = Lockstep::TickResolver::Resolve(match, {.orders = offered}, opened);
    Assert::AreEqual(std::size_t{1}, match.Proposals().size(), L"the offer never reached the table");

    Lockstep::OrderSet pull;
    pull.player = Lockstep::PlayerId{1};
    pull.withdrawals.push_back(Lockstep::WithdrawOrder{.proposal = match.Proposals().front().id});
    const std::vector<Lockstep::OrderSet> pulled = {pull};

    Lockstep::TickLog withdrawn;
    match = Lockstep::TickResolver::Resolve(match, {.orders = pulled}, withdrawn);

    const Lockstep::PlayerId seat{0};
    const Lockstep::MatchState state =
      Lockstep::ViewOf(Lockstep::Snapshot::For(match, seat), Lockstep::Snapshot::DigestFor(withdrawn, seat), 0);

    Assert::IsTrue(state.proposals.empty(), L"the withdrawn offer is still on the table");
    Assert::IsFalse(ActionOn(state, 0, Lockstep::EventActionKind::AcceptProposal).has_value(),
                    L"a withdrawn offer's card still offers an answer");
  }
};

} // namespace LockstepTests
