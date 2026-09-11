// DiplomacyTests.cpp -- trade lanes, proposals, and the two agreements nothing enforces.
//
// Step 6 of Design/Plans/4X-01-CoreLoop.md, and one test per lifecycle transition the one-pager
// names. The lifecycle is longer than it looks: propose, open for four ticks, withdraw, accept,
// decline, take effect at the accepting lock, carry a conditional order, be re-validated every
// lock, be voided with a reason on both sides, be reported as ignored, be cancelled by either
// party, and be cancelled by losing a system -- with the last two reading differently, because
// "the tell only works if the reader knows which".

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

/// Two neighbouring empires, and the lane between them.
struct Neighbors
{
  Lockstep::Match match;
  Lockstep::LaneId lane;
  Lockstep::SystemId mine;
  Lockstep::SystemId theirs;
};

/// Player 0 and player 1 holding the two ends of one lane.
///
/// Built by hand rather than played into, because reaching adjacency legitimately takes a dozen
/// ticks of expansion that this file is not about.
[[nodiscard]] Neighbors Adjacent()
{
  Lockstep::MatchRules rules;
  rules.playerCount = 6;
  Lockstep::Match match = Lockstep::Match::Create(rules, SEED);

  const Lockstep::SystemId capital = match.GalaxyGraph().Capitals()[0];
  const Lockstep::LaneId lane = match.GalaxyGraph().LanesAt(capital).front();
  const Lockstep::SystemId other = match.GalaxyGraph().OtherEnd(lane, capital);

  match.MutableSystems()[other.AsSize()].owner = Lockstep::PlayerId{1};

  // Enough in the purse that affordability is never what a test is measuring.
  for (Lockstep::PlayerState& player : match.MutablePlayers())
  {
    player.credits = 500;
  }

  return Neighbors{.match = match, .lane = lane, .mine = capital, .theirs = other};
}

[[nodiscard]] Lockstep::Match Advance(const Lockstep::Match& _match, std::span<const Lockstep::OrderSet> _orders, Lockstep::TickLog& _log)
{
  return Lockstep::TickResolver::Resolve(_match, {.orders = _orders}, _log);
}

[[nodiscard]] Lockstep::Match Advance(const Lockstep::Match& _match, std::span<const Lockstep::OrderSet> _orders)
{
  Lockstep::TickLog log;
  return Lockstep::TickResolver::Resolve(_match, {.orders = _orders}, log);
}

[[nodiscard]] Lockstep::Match Quiet(const Lockstep::Match& _match)
{
  return Advance(_match, {});
}

[[nodiscard]] const Lockstep::DigestEntry* Find(const Lockstep::TickLog& _log, std::int32_t _player, Lockstep::DigestKind _kind)
{
  const std::vector<Lockstep::DigestEntry>& digest = _log.digests[static_cast<std::size_t>(_player)];
  const auto found =
    std::find_if(digest.begin(), digest.end(), [_kind](const Lockstep::DigestEntry& _entry) { return _entry.kind == _kind; });
  return found == digest.end() ? nullptr : &*found;
}

/// Proposes from player 0 to player 1 and returns the state with the offer on the table.
[[nodiscard]] Lockstep::Match Propose(const Lockstep::Match& _match, Lockstep::ProposalOrder _proposal)
{
  Lockstep::OrderSet orders;
  orders.player = Lockstep::PlayerId{0};
  orders.proposals.push_back(_proposal);
  const std::array<Lockstep::OrderSet, 1> sets = {orders};
  return Advance(_match, sets);
}

[[nodiscard]] Lockstep::Match Answer(const Lockstep::Match& _match, Lockstep::Answer _answer, Lockstep::TickLog& _log)
{
  Lockstep::OrderSet orders;
  orders.player = Lockstep::PlayerId{1};
  orders.answers.push_back(Lockstep::AnswerOrder{.proposal = _match.Proposals().front().id, .answer = _answer});
  const std::array<Lockstep::OrderSet, 1> sets = {orders};
  return Advance(_match, sets, _log);
}

} // namespace

TEST_CLASS(TradeLaneTests)
{
public:
  // "A lane accepted at a lock opens in that same phase 1 and pays from that tick's production."
  // The lane is opened in phase 1 and production is phase 2, so it pays the tick it opens -- one
  // tick earlier than it would if opening were deferred, and the one-pager is specific about it.
  TEST_METHOD(AnAcceptedLanePaysInTheTickItWasAccepted)
  {
    Neighbors setup = Adjacent();
    setup.match = Propose(
      setup.match, Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::OpenLane, .lane = setup.lane});

    const std::uint32_t before = setup.match.PlayerAt(Lockstep::PlayerId{1}).credits;

    Lockstep::TickLog log;
    const Lockstep::Match after = Answer(setup.match, Lockstep::Answer::Accept, log);

    Assert::AreEqual(static_cast<size_t>(1), after.TradeLanes().size());
    const std::uint32_t gained = after.PlayerAt(Lockstep::PlayerId{1}).credits - before;
    Assert::IsTrue(gained >= after.Rules().tradeLaneIncome, L"it paid in the tick it opened");
  }

  TEST_METHOD(DecliningClosesTheOfferAndOpensNothing)
  {
    Neighbors setup = Adjacent();
    setup.match = Propose(
      setup.match, Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::OpenLane, .lane = setup.lane});

    Lockstep::TickLog log;
    const Lockstep::Match after = Answer(setup.match, Lockstep::Answer::Decline, log);

    Assert::IsTrue(after.TradeLanes().empty());
    Assert::IsTrue(after.Proposals().empty(), L"a declined offer is off the table");

    const Lockstep::DigestEntry* answered = Find(log, 0, Lockstep::DigestKind::ProposalAnswered);
    Assert::IsNotNull(answered);
    Assert::AreEqual(std::string("Proposal declined"), answered->title);
  }

  // "Either can cancel it at any tick."
  TEST_METHOD(EitherPartyMayCancelTheLane)
  {
    for (std::int32_t canceller : {0, 1})
    {
      Neighbors setup = Adjacent();
      setup.match.MutableTradeLanes().push_back(
        Lockstep::ActiveTradeLane{.lane = setup.lane, .a = Lockstep::PlayerId{0}, .b = Lockstep::PlayerId{1}});

      Lockstep::OrderSet orders;
      orders.player = Lockstep::PlayerId{canceller};
      orders.cancellations.push_back(Lockstep::CancelLaneOrder{.lane = setup.lane});
      const std::array<Lockstep::OrderSet, 1> sets = {orders};

      const Lockstep::Match after = Advance(setup.match, sets);
      Assert::IsTrue(after.TradeLanes().empty(), (std::wstring(L"player ") + std::to_wstring(canceller) + L" could not close it").c_str());
    }
  }

  TEST_METHOD(YouCannotCancelALaneYouAreNotOn)
  {
    Neighbors setup = Adjacent();
    setup.match.MutableTradeLanes().push_back(
      Lockstep::ActiveTradeLane{.lane = setup.lane, .a = Lockstep::PlayerId{0}, .b = Lockstep::PlayerId{1}});

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{3};
    orders.cancellations.push_back(Lockstep::CancelLaneOrder{.lane = setup.lane});

    const std::vector<Lockstep::RejectedOrder> rejected = setup.match.Validate(orders);
    Assert::AreEqual(static_cast<size_t>(1), rejected.size());
    Assert::IsTrue(rejected.front().reason == Lockstep::OrderRejection::NotYourTradeLane);

    const std::array<Lockstep::OrderSet, 1> sets = {orders};
    Assert::AreEqual(static_cast<size_t>(1), Advance(setup.match, sets).TradeLanes().size(), L"and the lane is still there");
  }

  // THE ONE-PAGER IS EMPHATIC ABOUT THIS ONE: "the digest distinguishes *cancelled by partner* from
  // *cancelled: system lost* -- the tell only works if the reader knows which."
  TEST_METHOD(CanceledByPartnerAndSystemLostReadDifferently)
  {
    // By partner.
    std::string byPartner;
    {
      Neighbors setup = Adjacent();
      setup.match.MutableTradeLanes().push_back(
        Lockstep::ActiveTradeLane{.lane = setup.lane, .a = Lockstep::PlayerId{0}, .b = Lockstep::PlayerId{1}});

      Lockstep::OrderSet orders;
      orders.player = Lockstep::PlayerId{1};
      orders.cancellations.push_back(Lockstep::CancelLaneOrder{.lane = setup.lane});
      const std::array<Lockstep::OrderSet, 1> sets = {orders};

      Lockstep::TickLog log;
      (void)Advance(setup.match, sets, log);

      const Lockstep::DigestEntry* entry = Find(log, 0, Lockstep::DigestKind::LaneCanceled);
      Assert::IsNotNull(entry, L"the abandoned partner is told");
      byPartner = entry->detail;
    }

    // System lost.
    std::string systemLost;
    {
      Neighbors setup = Adjacent();
      setup.match.SetTick(setup.match.Rules().capitalGuardTicks);
      setup.match.MutableTradeLanes().push_back(
        Lockstep::ActiveTradeLane{.lane = setup.lane, .a = Lockstep::PlayerId{0}, .b = Lockstep::PlayerId{1}});

      // Player 3 takes the far end: two uncontested ticks on an undefended system.
      Lockstep::MatchFleet raider;
      raider.owner = Lockstep::PlayerId{3};
      raider.ships = 50;
      raider.at = setup.theirs;
      (void)setup.match.AddFleet(raider);

      Lockstep::TickLog log;
      Lockstep::Match after = setup.match;
      for (std::int32_t tick = 0; tick < 2; ++tick)
      {
        after = Advance(after, {}, log);
      }

      Assert::IsTrue(after.TradeLanes().empty(), L"the lane went with the system");
      const Lockstep::DigestEntry* entry = Find(log, 0, Lockstep::DigestKind::LaneCanceled);
      Assert::IsNotNull(entry);
      systemLost = entry->detail;
    }

    Assert::AreNotEqual(byPartner, systemLost, L"a reader has to be able to tell which happened");
    Assert::IsTrue(byPartner.find("partner") != std::string::npos, L"one says a partner walked away");
    Assert::IsTrue(systemLost.find("lost") != std::string::npos, L"the other says a system fell");
  }
};

TEST_CLASS(ProposalLifecycleTests)
{
public:
  // "Every open proposal is re-validated at every lock against current state (endpoints still owned
  // by the two parties, still adjacent); one that fails is voided and both digests say why."
  TEST_METHOD(AProposalWhoseEndpointIsCapturedIsVoidedInBothDigests)
  {
    Neighbors setup = Adjacent();
    setup.match.SetTick(setup.match.Rules().capitalGuardTicks);
    setup.match = Propose(
      setup.match, Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::OpenLane, .lane = setup.lane});
    Assert::AreEqual(static_cast<size_t>(1), setup.match.Proposals().size());

    // Player 3 takes the far end while the offer sits there.
    Lockstep::MatchFleet raider;
    raider.owner = Lockstep::PlayerId{3};
    raider.ships = 50;
    raider.at = setup.theirs;
    (void)setup.match.AddFleet(raider);

    Lockstep::TickLog log;
    Lockstep::Match after = setup.match;
    for (std::int32_t tick = 0; tick < 3 && !after.Proposals().empty(); ++tick)
    {
      after = Advance(after, {}, log);
    }

    Assert::IsTrue(after.Proposals().empty(), L"the offer is gone");
    Assert::IsNotNull(Find(log, 0, Lockstep::DigestKind::ProposalVoided), L"the proposer is told why");
    Assert::IsNotNull(Find(log, 1, Lockstep::DigestKind::ProposalVoided), L"and so is the counterparty");
    Assert::IsTrue(after.TradeLanes().empty(), L"and no lane was opened on the way out");
  }

  // "Stays open for four ticks so every player sees it in at least one daily session." Counted in
  // ticks, which is the only clock the simulation has (R16).
  TEST_METHOD(TheWindowIsCountedInTicksAndIsExactlyFour)
  {
    Neighbors setup = Adjacent();
    const std::uint32_t window = setup.match.Rules().proposalWindowTicks;
    Assert::AreEqual(4U, window, L"the one-pager fixes it at four");

    Lockstep::Match match =
      Propose(setup.match, Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::ShareScouting});

    // It is open for the whole window and gone on the tick after.
    for (std::uint32_t tick = 1; tick < window; ++tick)
    {
      Assert::AreEqual(static_cast<size_t>(1), match.Proposals().size(),
                       (std::wstring(L"still open at tick ") + std::to_wstring(match.Tick())).c_str());
      match = Quiet(match);
    }

    Lockstep::TickLog log;
    match = Advance(match, {}, log);
    Assert::IsTrue(match.Proposals().empty(), L"and closed once the window has run");
    Assert::IsNotNull(Find(log, 0, Lockstep::DigestKind::ProposalIgnored), L"reported to the proposer as ignored");
  }

  // "It can carry a conditional order -- if accepted, open lane -- so the effect lands without a
  // second round trip."
  TEST_METHOD(AConditionalLaneOpensWithTheAcceptance)
  {
    Neighbors setup = Adjacent();
    setup.match = Propose(
      setup.match,
      Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::ShareScouting, .conditionalLane = setup.lane});

    Lockstep::TickLog log;
    const Lockstep::Match after = Answer(setup.match, Lockstep::Answer::Accept, log);

    Assert::AreEqual(static_cast<size_t>(1), after.TradeLanes().size(), L"one tap, one tick, two effects");
    Assert::AreEqual(static_cast<size_t>(1), after.Agreements().size());
    Assert::IsNotNull(Find(log, 1, Lockstep::DigestKind::LaneOpened));
  }

  TEST_METHOD(ADeclinedProposalCarriesNoConditionalLane)
  {
    Neighbors setup = Adjacent();
    setup.match = Propose(
      setup.match,
      Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::ShareScouting, .conditionalLane = setup.lane});

    Lockstep::TickLog log;
    const Lockstep::Match after = Answer(setup.match, Lockstep::Answer::Decline, log);

    Assert::IsTrue(after.TradeLanes().empty(), L"conditional means conditional");
    Assert::IsTrue(after.Agreements().empty());
  }

  TEST_METHOD(AConditionalLaneMustJoinTheTwoEmpires)
  {
    const Neighbors setup = Adjacent();

    // A lane somewhere else entirely.
    Lockstep::LaneId elsewhere;
    for (std::size_t index = 0; index < setup.match.GalaxyGraph().Lanes().size(); ++index)
    {
      if (Lockstep::LaneId{static_cast<std::int32_t>(index)} != setup.lane)
      {
        elsewhere = Lockstep::LaneId{static_cast<std::int32_t>(index)};
        break;
      }
    }

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.proposals.push_back(
      Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::ShareScouting, .conditionalLane = elsewhere});

    const std::vector<Lockstep::RejectedOrder> rejected = setup.match.Validate(orders);
    Assert::AreEqual(static_cast<size_t>(1), rejected.size());
    Assert::IsTrue(rejected.front().reason == Lockstep::OrderRejection::ConditionalLaneNotBetweenYou);
  }
};

// The two kinds of offer that are recorded and NOT enforced. "No enforced treaties" is the reason
// the trade lane is called the one consensual mechanic, and these are what that costs.
TEST_CLASS(AgreementTests)
{
public:
  TEST_METHOD(SharedScoutingIsRecordedAndDoesNotExpire)
  {
    Neighbors setup = Adjacent();
    setup.match = Propose(setup.match, Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::ShareScouting});

    Lockstep::TickLog log;
    Lockstep::Match after = Answer(setup.match, Lockstep::Answer::Accept, log);

    Assert::AreEqual(static_cast<size_t>(1), after.Agreements().size());
    Assert::IsTrue(after.Agreements().front().kind == Lockstep::AgreementKind::ShareScouting);
    Assert::AreEqual(0U, after.Agreements().front().expiresAt, L"it runs until somebody stops it");
    Assert::IsNotNull(Find(log, 0, Lockstep::DigestKind::AgreementOpened));
    Assert::IsNotNull(Find(log, 1, Lockstep::DigestKind::AgreementOpened));

    for (std::int32_t tick = 0; tick < 8; ++tick)
    {
      after = Quiet(after);
    }
    Assert::AreEqual(static_cast<size_t>(1), after.Agreements().size(), L"and it is still there eight ticks later");

    // Fog itself is Step 8. What Step 6 owes is the record.
    Assert::IsTrue(after.HasAgreement(Lockstep::AgreementKind::ShareScouting, Lockstep::PlayerId{0}, Lockstep::PlayerId{1}));
  }

  TEST_METHOD(AHoldRunsForItsTicksAndThenLapsesQuietly)
  {
    Neighbors setup = Adjacent();
    setup.match =
      Propose(setup.match, Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::HoldForTicks, .ticks = 2});

    Lockstep::TickLog log;
    Lockstep::Match after = Answer(setup.match, Lockstep::Answer::Accept, log);

    Assert::AreEqual(static_cast<size_t>(1), after.Agreements().size());
    Assert::IsTrue(after.Agreements().front().kind == Lockstep::AgreementKind::HoldFire);
    Assert::IsTrue(after.Agreements().front().expiresAt > after.Tick());

    for (std::int32_t tick = 0; tick < 4 && !after.Agreements().empty(); ++tick)
    {
      after = Quiet(after);
    }
    Assert::IsTrue(after.Agreements().empty(), L"it ran out");
  }

  // The whole point, stated plainly: a hold is not enforced. Breaking one is possible, and the
  // only thing that happens is that everybody is told.
  TEST_METHOD(BreakingAHoldIsReportedAndNotPrevented)
  {
    Neighbors setup = Adjacent();
    setup.match.SetTick(setup.match.Rules().capitalGuardTicks);
    setup.match.MutableAgreements().push_back(Lockstep::Agreement{
      .kind = Lockstep::AgreementKind::HoldFire, .a = Lockstep::PlayerId{0}, .b = Lockstep::PlayerId{1}, .expiresAt = 99});

    Lockstep::MatchFleet aggressor;
    aggressor.owner = Lockstep::PlayerId{0};
    aggressor.ships = 30;
    aggressor.at = setup.theirs;
    const Lockstep::FleetId attacker = setup.match.AddFleet(aggressor);

    Lockstep::MatchFleet garrison;
    garrison.owner = Lockstep::PlayerId{1};
    garrison.ships = 10;
    garrison.at = setup.theirs;
    const Lockstep::FleetId defender = setup.match.AddFleet(garrison);

    Lockstep::TickLog log;
    const Lockstep::Match after = Advance(setup.match, {}, log);

    Assert::IsTrue(after.FleetAt(defender).ships < 10U, L"the attack went ahead -- nothing enforces a hold");
    Assert::IsTrue(after.FleetAt(attacker).ships > 0U);
    Assert::IsNotNull(Find(log, 0, Lockstep::DigestKind::AgreementBreached), L"the breaker is named to themselves");
    Assert::IsNotNull(Find(log, 1, Lockstep::DigestKind::AgreementBreached), L"and to the party they broke it with");
  }

  TEST_METHOD(AFightWithNoHoldInPlaceIsNotABreach)
  {
    Neighbors setup = Adjacent();
    setup.match.SetTick(setup.match.Rules().capitalGuardTicks);

    Lockstep::MatchFleet aggressor;
    aggressor.owner = Lockstep::PlayerId{0};
    aggressor.ships = 30;
    aggressor.at = setup.theirs;
    (void)setup.match.AddFleet(aggressor);

    Lockstep::MatchFleet garrison;
    garrison.owner = Lockstep::PlayerId{1};
    garrison.ships = 10;
    garrison.at = setup.theirs;
    (void)setup.match.AddFleet(garrison);

    Lockstep::TickLog log;
    (void)Advance(setup.match, {}, log);

    Assert::IsNull(Find(log, 0, Lockstep::DigestKind::AgreementBreached));
  }
};

// "First contact raises the game's own prompt: 'Contact: [player]. Propose trade lane?' -- one tap,
// no text."
TEST_CLASS(FirstContactTests)
{
public:
  TEST_METHOD(ContactIsRaisedWhenTwoEmpiresBecomeAdjacent)
  {
    Neighbors setup = Adjacent();

    // `Adjacent` places them side by side without playing a tick, so they have not met yet.
    Assert::IsFalse(setup.match.HaveMet(Lockstep::PlayerId{0}, Lockstep::PlayerId{1}));

    Lockstep::TickLog log;
    const Lockstep::Match after = Advance(setup.match, {}, log);

    Assert::IsTrue(after.HaveMet(Lockstep::PlayerId{0}, Lockstep::PlayerId{1}));

    const Lockstep::DigestEntry* mine = Find(log, 0, Lockstep::DigestKind::Contact);
    const Lockstep::DigestEntry* theirs = Find(log, 1, Lockstep::DigestKind::Contact);
    Assert::IsNotNull(mine, L"both sides are told");
    Assert::IsNotNull(theirs);
    Assert::AreEqual(std::string("Propose trade lane?"), mine->detail, L"one tap, no text");
    Assert::IsTrue(mine->other == Lockstep::PlayerId{1});
    Assert::IsTrue(theirs->other == Lockstep::PlayerId{0});
  }

  // Raised ONCE. A prompt that arrives every six hours for the rest of the match is a notification
  // stream, and the one-pager's design rule is one digest per tick and never one per event.
  TEST_METHOD(ContactIsRaisedOnceAndNotEveryTickAfter)
  {
    Neighbors setup = Adjacent();

    Lockstep::TickLog first;
    Lockstep::Match after = Advance(setup.match, {}, first);
    Assert::IsNotNull(Find(first, 0, Lockstep::DigestKind::Contact));

    for (std::int32_t tick = 0; tick < 3; ++tick)
    {
      Lockstep::TickLog later;
      after = Advance(after, {}, later);
      Assert::IsNull(Find(later, 0, Lockstep::DigestKind::Contact),
                     (std::wstring(L"raised again at tick ") + std::to_wstring(after.Tick())).c_str());
    }

    Assert::AreEqual(static_cast<size_t>(1), after.Contacts().size());
  }

  TEST_METHOD(ContactIsRecordedWithOneOrderingPerPair)
  {
    Neighbors setup = Adjacent();
    const Lockstep::Match after = Quiet(setup.match);

    Assert::AreEqual(static_cast<size_t>(1), after.Contacts().size());
    Assert::IsTrue(after.Contacts().front().a < after.Contacts().front().b, L"the lower id first, so a pair has one form");
    Assert::IsTrue(after.HaveMet(Lockstep::PlayerId{1}, Lockstep::PlayerId{0}), L"asked either way round");
  }
};

} // namespace GameLogicTests
