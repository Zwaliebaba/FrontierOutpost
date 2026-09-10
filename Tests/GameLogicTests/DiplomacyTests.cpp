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
  Frontier::Match match;
  Frontier::LaneId lane;
  Frontier::SystemId mine;
  Frontier::SystemId theirs;
};

/// Player 0 and player 1 holding the two ends of one lane.
///
/// Built by hand rather than played into, because reaching adjacency legitimately takes a dozen
/// ticks of expansion that this file is not about.
[[nodiscard]] Neighbors Adjacent()
{
  Frontier::MatchRules rules;
  rules.playerCount = 6;
  Frontier::Match match = Frontier::Match::Create(rules, SEED);

  const Frontier::SystemId capital = match.GalaxyGraph().Capitals()[0];
  const Frontier::LaneId lane = match.GalaxyGraph().LanesAt(capital).front();
  const Frontier::SystemId other = match.GalaxyGraph().OtherEnd(lane, capital);

  match.MutableSystems()[other.AsSize()].owner = Frontier::PlayerId{1};

  // Enough in the purse that affordability is never what a test is measuring.
  for (Frontier::PlayerState& player : match.MutablePlayers())
  {
    player.credits = 500;
  }

  return Neighbors{.match = match, .lane = lane, .mine = capital, .theirs = other};
}

[[nodiscard]] Frontier::Match Advance(const Frontier::Match& _match, std::span<const Frontier::OrderSet> _orders, Frontier::TickLog& _log)
{
  return Frontier::TickResolver::Resolve(_match, _orders, _log);
}

[[nodiscard]] Frontier::Match Advance(const Frontier::Match& _match, std::span<const Frontier::OrderSet> _orders)
{
  Frontier::TickLog log;
  return Frontier::TickResolver::Resolve(_match, _orders, log);
}

[[nodiscard]] Frontier::Match Quiet(const Frontier::Match& _match)
{
  return Advance(_match, {});
}

[[nodiscard]] const Frontier::DigestEntry* Find(const Frontier::TickLog& _log, std::int32_t _player, Frontier::DigestKind _kind)
{
  const std::vector<Frontier::DigestEntry>& digest = _log.digests[static_cast<std::size_t>(_player)];
  const auto found =
    std::find_if(digest.begin(), digest.end(), [_kind](const Frontier::DigestEntry& _entry) { return _entry.kind == _kind; });
  return found == digest.end() ? nullptr : &*found;
}

/// Proposes from player 0 to player 1 and returns the state with the offer on the table.
[[nodiscard]] Frontier::Match Propose(const Frontier::Match& _match, Frontier::ProposalOrder _proposal)
{
  Frontier::OrderSet orders;
  orders.player = Frontier::PlayerId{0};
  orders.proposals.push_back(_proposal);
  const std::array<Frontier::OrderSet, 1> sets = {orders};
  return Advance(_match, sets);
}

[[nodiscard]] Frontier::Match Answer(const Frontier::Match& _match, Frontier::Answer _answer, Frontier::TickLog& _log)
{
  Frontier::OrderSet orders;
  orders.player = Frontier::PlayerId{1};
  orders.answers.push_back(Frontier::AnswerOrder{.proposal = _match.Proposals().front().id, .answer = _answer});
  const std::array<Frontier::OrderSet, 1> sets = {orders};
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
      setup.match, Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::OpenLane, .lane = setup.lane});

    const std::uint32_t before = setup.match.PlayerAt(Frontier::PlayerId{1}).credits;

    Frontier::TickLog log;
    const Frontier::Match after = Answer(setup.match, Frontier::Answer::Accept, log);

    Assert::AreEqual(static_cast<size_t>(1), after.TradeLanes().size());
    const std::uint32_t gained = after.PlayerAt(Frontier::PlayerId{1}).credits - before;
    Assert::IsTrue(gained >= after.Rules().tradeLaneIncome, L"it paid in the tick it opened");
  }

  TEST_METHOD(DecliningClosesTheOfferAndOpensNothing)
  {
    Neighbors setup = Adjacent();
    setup.match = Propose(
      setup.match, Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::OpenLane, .lane = setup.lane});

    Frontier::TickLog log;
    const Frontier::Match after = Answer(setup.match, Frontier::Answer::Decline, log);

    Assert::IsTrue(after.TradeLanes().empty());
    Assert::IsTrue(after.Proposals().empty(), L"a declined offer is off the table");

    const Frontier::DigestEntry* answered = Find(log, 0, Frontier::DigestKind::ProposalAnswered);
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
        Frontier::ActiveTradeLane{.lane = setup.lane, .a = Frontier::PlayerId{0}, .b = Frontier::PlayerId{1}});

      Frontier::OrderSet orders;
      orders.player = Frontier::PlayerId{canceller};
      orders.cancellations.push_back(Frontier::CancelLaneOrder{.lane = setup.lane});
      const std::array<Frontier::OrderSet, 1> sets = {orders};

      const Frontier::Match after = Advance(setup.match, sets);
      Assert::IsTrue(after.TradeLanes().empty(), (std::wstring(L"player ") + std::to_wstring(canceller) + L" could not close it").c_str());
    }
  }

  TEST_METHOD(YouCannotCancelALaneYouAreNotOn)
  {
    Neighbors setup = Adjacent();
    setup.match.MutableTradeLanes().push_back(
      Frontier::ActiveTradeLane{.lane = setup.lane, .a = Frontier::PlayerId{0}, .b = Frontier::PlayerId{1}});

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{3};
    orders.cancellations.push_back(Frontier::CancelLaneOrder{.lane = setup.lane});

    const std::vector<Frontier::RejectedOrder> rejected = setup.match.Validate(orders);
    Assert::AreEqual(static_cast<size_t>(1), rejected.size());
    Assert::IsTrue(rejected.front().reason == Frontier::OrderRejection::NotYourTradeLane);

    const std::array<Frontier::OrderSet, 1> sets = {orders};
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
        Frontier::ActiveTradeLane{.lane = setup.lane, .a = Frontier::PlayerId{0}, .b = Frontier::PlayerId{1}});

      Frontier::OrderSet orders;
      orders.player = Frontier::PlayerId{1};
      orders.cancellations.push_back(Frontier::CancelLaneOrder{.lane = setup.lane});
      const std::array<Frontier::OrderSet, 1> sets = {orders};

      Frontier::TickLog log;
      (void)Advance(setup.match, sets, log);

      const Frontier::DigestEntry* entry = Find(log, 0, Frontier::DigestKind::LaneCanceled);
      Assert::IsNotNull(entry, L"the abandoned partner is told");
      byPartner = entry->detail;
    }

    // System lost.
    std::string systemLost;
    {
      Neighbors setup = Adjacent();
      setup.match.SetTick(setup.match.Rules().capitalGuardTicks);
      setup.match.MutableTradeLanes().push_back(
        Frontier::ActiveTradeLane{.lane = setup.lane, .a = Frontier::PlayerId{0}, .b = Frontier::PlayerId{1}});

      // Player 3 takes the far end: two uncontested ticks on an undefended system.
      Frontier::MatchFleet raider;
      raider.owner = Frontier::PlayerId{3};
      raider.ships = 50;
      raider.at = setup.theirs;
      (void)setup.match.AddFleet(raider);

      Frontier::TickLog log;
      Frontier::Match after = setup.match;
      for (std::int32_t tick = 0; tick < 2; ++tick)
      {
        after = Advance(after, {}, log);
      }

      Assert::IsTrue(after.TradeLanes().empty(), L"the lane went with the system");
      const Frontier::DigestEntry* entry = Find(log, 0, Frontier::DigestKind::LaneCanceled);
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
      setup.match, Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::OpenLane, .lane = setup.lane});
    Assert::AreEqual(static_cast<size_t>(1), setup.match.Proposals().size());

    // Player 3 takes the far end while the offer sits there.
    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{3};
    raider.ships = 50;
    raider.at = setup.theirs;
    (void)setup.match.AddFleet(raider);

    Frontier::TickLog log;
    Frontier::Match after = setup.match;
    for (std::int32_t tick = 0; tick < 3 && !after.Proposals().empty(); ++tick)
    {
      after = Advance(after, {}, log);
    }

    Assert::IsTrue(after.Proposals().empty(), L"the offer is gone");
    Assert::IsNotNull(Find(log, 0, Frontier::DigestKind::ProposalVoided), L"the proposer is told why");
    Assert::IsNotNull(Find(log, 1, Frontier::DigestKind::ProposalVoided), L"and so is the counterparty");
    Assert::IsTrue(after.TradeLanes().empty(), L"and no lane was opened on the way out");
  }

  // "Stays open for four ticks so every player sees it in at least one daily session." Counted in
  // ticks, which is the only clock the simulation has (R16).
  TEST_METHOD(TheWindowIsCountedInTicksAndIsExactlyFour)
  {
    Neighbors setup = Adjacent();
    const std::uint32_t window = setup.match.Rules().proposalWindowTicks;
    Assert::AreEqual(4U, window, L"the one-pager fixes it at four");

    Frontier::Match match =
      Propose(setup.match, Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::ShareScouting});

    // It is open for the whole window and gone on the tick after.
    for (std::uint32_t tick = 1; tick < window; ++tick)
    {
      Assert::AreEqual(static_cast<size_t>(1), match.Proposals().size(),
                       (std::wstring(L"still open at tick ") + std::to_wstring(match.Tick())).c_str());
      match = Quiet(match);
    }

    Frontier::TickLog log;
    match = Advance(match, {}, log);
    Assert::IsTrue(match.Proposals().empty(), L"and closed once the window has run");
    Assert::IsNotNull(Find(log, 0, Frontier::DigestKind::ProposalIgnored), L"reported to the proposer as ignored");
  }

  // "It can carry a conditional order -- if accepted, open lane -- so the effect lands without a
  // second round trip."
  TEST_METHOD(AConditionalLaneOpensWithTheAcceptance)
  {
    Neighbors setup = Adjacent();
    setup.match = Propose(
      setup.match,
      Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::ShareScouting, .conditionalLane = setup.lane});

    Frontier::TickLog log;
    const Frontier::Match after = Answer(setup.match, Frontier::Answer::Accept, log);

    Assert::AreEqual(static_cast<size_t>(1), after.TradeLanes().size(), L"one tap, one tick, two effects");
    Assert::AreEqual(static_cast<size_t>(1), after.Agreements().size());
    Assert::IsNotNull(Find(log, 1, Frontier::DigestKind::LaneOpened));
  }

  TEST_METHOD(ADeclinedProposalCarriesNoConditionalLane)
  {
    Neighbors setup = Adjacent();
    setup.match = Propose(
      setup.match,
      Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::ShareScouting, .conditionalLane = setup.lane});

    Frontier::TickLog log;
    const Frontier::Match after = Answer(setup.match, Frontier::Answer::Decline, log);

    Assert::IsTrue(after.TradeLanes().empty(), L"conditional means conditional");
    Assert::IsTrue(after.Agreements().empty());
  }

  TEST_METHOD(AConditionalLaneMustJoinTheTwoEmpires)
  {
    const Neighbors setup = Adjacent();

    // A lane somewhere else entirely.
    Frontier::LaneId elsewhere;
    for (std::size_t index = 0; index < setup.match.GalaxyGraph().Lanes().size(); ++index)
    {
      if (Frontier::LaneId{static_cast<std::int32_t>(index)} != setup.lane)
      {
        elsewhere = Frontier::LaneId{static_cast<std::int32_t>(index)};
        break;
      }
    }

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.proposals.push_back(
      Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::ShareScouting, .conditionalLane = elsewhere});

    const std::vector<Frontier::RejectedOrder> rejected = setup.match.Validate(orders);
    Assert::AreEqual(static_cast<size_t>(1), rejected.size());
    Assert::IsTrue(rejected.front().reason == Frontier::OrderRejection::ConditionalLaneNotBetweenYou);
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
    setup.match = Propose(setup.match, Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::ShareScouting});

    Frontier::TickLog log;
    Frontier::Match after = Answer(setup.match, Frontier::Answer::Accept, log);

    Assert::AreEqual(static_cast<size_t>(1), after.Agreements().size());
    Assert::IsTrue(after.Agreements().front().kind == Frontier::AgreementKind::ShareScouting);
    Assert::AreEqual(0U, after.Agreements().front().expiresAt, L"it runs until somebody stops it");
    Assert::IsNotNull(Find(log, 0, Frontier::DigestKind::AgreementOpened));
    Assert::IsNotNull(Find(log, 1, Frontier::DigestKind::AgreementOpened));

    for (std::int32_t tick = 0; tick < 8; ++tick)
    {
      after = Quiet(after);
    }
    Assert::AreEqual(static_cast<size_t>(1), after.Agreements().size(), L"and it is still there eight ticks later");

    // Fog itself is Step 8. What Step 6 owes is the record.
    Assert::IsTrue(after.HasAgreement(Frontier::AgreementKind::ShareScouting, Frontier::PlayerId{0}, Frontier::PlayerId{1}));
  }

  TEST_METHOD(AHoldRunsForItsTicksAndThenLapsesQuietly)
  {
    Neighbors setup = Adjacent();
    setup.match =
      Propose(setup.match, Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::HoldForTicks, .ticks = 2});

    Frontier::TickLog log;
    Frontier::Match after = Answer(setup.match, Frontier::Answer::Accept, log);

    Assert::AreEqual(static_cast<size_t>(1), after.Agreements().size());
    Assert::IsTrue(after.Agreements().front().kind == Frontier::AgreementKind::HoldFire);
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
    setup.match.MutableAgreements().push_back(Frontier::Agreement{
      .kind = Frontier::AgreementKind::HoldFire, .a = Frontier::PlayerId{0}, .b = Frontier::PlayerId{1}, .expiresAt = 99});

    Frontier::MatchFleet aggressor;
    aggressor.owner = Frontier::PlayerId{0};
    aggressor.ships = 30;
    aggressor.at = setup.theirs;
    const Frontier::FleetId attacker = setup.match.AddFleet(aggressor);

    Frontier::MatchFleet garrison;
    garrison.owner = Frontier::PlayerId{1};
    garrison.ships = 10;
    garrison.at = setup.theirs;
    const Frontier::FleetId defender = setup.match.AddFleet(garrison);

    Frontier::TickLog log;
    const Frontier::Match after = Advance(setup.match, {}, log);

    Assert::IsTrue(after.FleetAt(defender).ships < 10U, L"the attack went ahead -- nothing enforces a hold");
    Assert::IsTrue(after.FleetAt(attacker).ships > 0U);
    Assert::IsNotNull(Find(log, 0, Frontier::DigestKind::AgreementBreached), L"the breaker is named to themselves");
    Assert::IsNotNull(Find(log, 1, Frontier::DigestKind::AgreementBreached), L"and to the party they broke it with");
  }

  TEST_METHOD(AFightWithNoHoldInPlaceIsNotABreach)
  {
    Neighbors setup = Adjacent();
    setup.match.SetTick(setup.match.Rules().capitalGuardTicks);

    Frontier::MatchFleet aggressor;
    aggressor.owner = Frontier::PlayerId{0};
    aggressor.ships = 30;
    aggressor.at = setup.theirs;
    (void)setup.match.AddFleet(aggressor);

    Frontier::MatchFleet garrison;
    garrison.owner = Frontier::PlayerId{1};
    garrison.ships = 10;
    garrison.at = setup.theirs;
    (void)setup.match.AddFleet(garrison);

    Frontier::TickLog log;
    (void)Advance(setup.match, {}, log);

    Assert::IsNull(Find(log, 0, Frontier::DigestKind::AgreementBreached));
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
    Assert::IsFalse(setup.match.HaveMet(Frontier::PlayerId{0}, Frontier::PlayerId{1}));

    Frontier::TickLog log;
    const Frontier::Match after = Advance(setup.match, {}, log);

    Assert::IsTrue(after.HaveMet(Frontier::PlayerId{0}, Frontier::PlayerId{1}));

    const Frontier::DigestEntry* mine = Find(log, 0, Frontier::DigestKind::Contact);
    const Frontier::DigestEntry* theirs = Find(log, 1, Frontier::DigestKind::Contact);
    Assert::IsNotNull(mine, L"both sides are told");
    Assert::IsNotNull(theirs);
    Assert::AreEqual(std::string("Propose trade lane?"), mine->detail, L"one tap, no text");
    Assert::IsTrue(mine->other == Frontier::PlayerId{1});
    Assert::IsTrue(theirs->other == Frontier::PlayerId{0});
  }

  // Raised ONCE. A prompt that arrives every six hours for the rest of the match is a notification
  // stream, and the one-pager's design rule is one digest per tick and never one per event.
  TEST_METHOD(ContactIsRaisedOnceAndNotEveryTickAfter)
  {
    Neighbors setup = Adjacent();

    Frontier::TickLog first;
    Frontier::Match after = Advance(setup.match, {}, first);
    Assert::IsNotNull(Find(first, 0, Frontier::DigestKind::Contact));

    for (std::int32_t tick = 0; tick < 3; ++tick)
    {
      Frontier::TickLog later;
      after = Advance(after, {}, later);
      Assert::IsNull(Find(later, 0, Frontier::DigestKind::Contact),
                     (std::wstring(L"raised again at tick ") + std::to_wstring(after.Tick())).c_str());
    }

    Assert::AreEqual(static_cast<size_t>(1), after.Contacts().size());
  }

  TEST_METHOD(ContactIsRecordedWithOneOrderingPerPair)
  {
    Neighbors setup = Adjacent();
    const Frontier::Match after = Quiet(setup.match);

    Assert::AreEqual(static_cast<size_t>(1), after.Contacts().size());
    Assert::IsTrue(after.Contacts().front().a < after.Contacts().front().b, L"the lower id first, so a pair has one form");
    Assert::IsTrue(after.HaveMet(Frontier::PlayerId{1}, Frontier::PlayerId{0}), L"asked either way round");
  }
};

} // namespace GameLogicTests
