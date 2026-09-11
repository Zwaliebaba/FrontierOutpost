// MatchTests.cpp -- the match state, the orders that reach it, and the tick that resolves them.
//
// Steps 3 and 4 of Design/Plans/4X-01-CoreLoop.md. GameLogicTests.cpp holds the galaxy; this file
// holds everything built on top of it.
//
// MOST OF THESE BUILD THE STATE THEY WANT DIRECTLY rather than playing toward it. A capture takes
// two consecutive uncontested ticks, and reaching one by issuing legal orders from tick zero would
// take a dozen ticks of setup in which anything could go wrong for a reason the test is not about.
// `Match` exposes its vectors for exactly this: a test that puts a fleet where it wants it is
// testing the rule, not the route.

#include "pch.h"
#include "CppUnitTest.h"

#include "TickResolver.h"

#include "ByteReader.h"
#include "ByteWriter.h"

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

[[nodiscard]] Lockstep::Match SixPlayerMatch()
{
  Lockstep::MatchRules rules;
  rules.playerCount = 6;
  return Lockstep::Match::Create(rules, SEED);
}

/// The capital of player n, which is where its starting fleet is.
[[nodiscard]] Lockstep::SystemId CapitalOf(const Lockstep::Match& _match, std::int32_t _player)
{
  return _match.GalaxyGraph().Capitals()[static_cast<std::size_t>(_player)];
}

/// A system one lane from `_from`, and the lane's cost.
[[nodiscard]] Lockstep::SystemId NeighborOf(const Lockstep::Match& _match, Lockstep::SystemId _from)
{
  const Lockstep::LaneId lane = _match.GalaxyGraph().LanesAt(_from).front();
  return _match.GalaxyGraph().OtherEnd(lane, _from);
}

[[nodiscard]] Lockstep::FleetId FleetOf(const Lockstep::Match& _match, std::int32_t _player)
{
  for (std::size_t index = 0; index < _match.Fleets().size(); ++index)
  {
    if (_match.Fleets()[index].owner == Lockstep::PlayerId{_player} && !_match.Fleets()[index].destroyed)
    {
      return Lockstep::FleetId{static_cast<std::int32_t>(index)};
    }
  }
  return Lockstep::FleetId{};
}

/// Resolves one tick with the given order sets and throws the log away.
[[nodiscard]] Lockstep::Match Advance(const Lockstep::Match& _match, std::span<const Lockstep::OrderSet> _orders)
{
  Lockstep::TickLog log;
  return Lockstep::TickResolver::Resolve(_match, {.orders = _orders}, log);
}

[[nodiscard]] Lockstep::Match AdvanceQuietly(const Lockstep::Match& _match)
{
  return Advance(_match, {});
}

/// Every reason in a rejection list, so a test can say what it expected rather than an index.
[[nodiscard]] bool Refused(const std::vector<Lockstep::RejectedOrder>& _rejected, Lockstep::OrderRejection _reason)
{
  return std::any_of(_rejected.begin(), _rejected.end(),
                     [_reason](const Lockstep::RejectedOrder& _refusal) { return _refusal.reason == _reason; });
}

[[nodiscard]] bool AnyLineContains(const Lockstep::TickLog& _log, std::string_view _fragment)
{
  const std::vector<std::string> lines = _log.AllLines();
  return std::any_of(lines.begin(), lines.end(),
                     [_fragment](const std::string& _line) { return _line.find(_fragment) != std::string::npos; });
}

[[nodiscard]] bool HasDigestKind(const Lockstep::TickLog& _log, std::int32_t _player, Lockstep::DigestKind _kind)
{
  const std::vector<Lockstep::DigestEntry>& digest = _log.digests[static_cast<std::size_t>(_player)];
  return std::any_of(digest.begin(), digest.end(), [_kind](const Lockstep::DigestEntry& _entry) { return _entry.kind == _kind; });
}

} // namespace

// A match is a value: created from rules and a seed, hashable, and self-consistent.
TEST_CLASS(MatchStateTests)
{
public:
  TEST_METHOD(EveryPlayerStartsOnACapitalWithAFleet)
  {
    const Lockstep::Match match = SixPlayerMatch();

    Assert::AreEqual(static_cast<size_t>(6), match.Players().size());
    Assert::AreEqual(static_cast<size_t>(6), match.Fleets().size(), L"one starting fleet each");
    Assert::AreEqual(0U, match.Tick());
    Assert::IsTrue(match.IsConsistent());

    for (std::int32_t player = 0; player < 6; ++player)
    {
      const Lockstep::SystemId capital = CapitalOf(match, player);
      Assert::IsTrue(match.SystemAt(capital).owner == Lockstep::PlayerId{player}, L"a capital starts held by its player");
      Assert::AreEqual(match.Rules().startingCredits, match.PlayerAt(Lockstep::PlayerId{player}).credits);

      const Lockstep::FleetId fleet = FleetOf(match, player);
      Assert::IsTrue(fleet.IsValid());
      Assert::IsTrue(match.FleetAt(fleet).at == capital, L"the fleet starts on the capital");
      Assert::IsFalse(match.FleetAt(fleet).InTransit());
    }
  }

  TEST_METHOD(NothingButCapitalsIsOwnedAtTickZero)
  {
    const Lockstep::Match match = SixPlayerMatch();

    std::uint32_t owned = 0;
    for (const Lockstep::SystemState& system : match.Systems())
    {
      if (system.owner.IsValid())
      {
        ++owned;
      }
    }
    Assert::AreEqual(6U, owned, L"six capitals and nothing else");
  }

  // The determinism test at the level a match cares about (ADR-018).
  TEST_METHOD(TheSameSeedGivesTheSameMatch)
  {
    Assert::AreEqual(SixPlayerMatch().Hash(), SixPlayerMatch().Hash());

    Lockstep::MatchRules other;
    other.playerCount = 6;
    Assert::AreNotEqual(SixPlayerMatch().Hash(), Lockstep::Match::Create(other, SEED + 1).Hash(), L"a different seed is a different match");
  }

  // Every field a rule reads has to be in the hash, or it can drift without a test noticing. These
  // check the ones a resolver phase writes.
  TEST_METHOD(TheHashCoversWhatTheResolverWrites)
  {
    const Lockstep::Match base = SixPlayerMatch();

    const auto changed = [&base](auto _mutate)
    {
      Lockstep::Match copy = base;
      _mutate(copy);
      return copy.Hash() != base.Hash();
    };

    Assert::IsTrue(changed([](Lockstep::Match& _m) { _m.MutablePlayers()[0].credits += 1; }), L"credits");
    Assert::IsTrue(changed([](Lockstep::Match& _m) { _m.MutablePlayers()[0].conceded = true; }), L"concession");
    Assert::IsTrue(changed([](Lockstep::Match& _m) { _m.MutableSystems()[1].hasShipyard = true; }), L"buildings");
    Assert::IsTrue(changed([](Lockstep::Match& _m) { _m.MutableSystems()[1].siegeTicks = 1; }), L"siege progress");
    Assert::IsTrue(changed([](Lockstep::Match& _m) { _m.MutableFleets()[0].ships += 1; }), L"fleet strength");
    Assert::IsTrue(changed([](Lockstep::Match& _m) { _m.MutableFleets()[0].orderedTo = Lockstep::SystemId{3}; }), L"pending orders");
    Assert::IsTrue(changed([](Lockstep::Match& _m) { _m.SetTick(5); }), L"the tick");
  }

  TEST_METHOD(ACapitalIsGuardedForTheFirstTwelveTicks)
  {
    Lockstep::Match match = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(match, 0);

    Assert::IsTrue(match.IsCapitalGuarded(capital), L"guarded at tick zero");

    match.SetTick(match.Rules().capitalGuardTicks - 1);
    Assert::IsTrue(match.IsCapitalGuarded(capital), L"still guarded the tick before it lapses");

    match.SetTick(match.Rules().capitalGuardTicks);
    Assert::IsFalse(match.IsCapitalGuarded(capital), L"and open from then on");

    // The guard is a capital rule, not a general one.
    Assert::IsFalse(match.IsCapitalGuarded(NeighborOf(match, capital)));
  }

  // Rules that describe a game nobody could play.
  //
  // Tested through `Check` rather than by calling `Match::Create` and expecting it to fail.
  // `Neuron::Fatal` calls `__debugbreak()` before it throws, and a breakpoint inside the unit-test
  // host does not raise an exception the harness can catch -- an earlier version of this test
  // reported "Skipped", which is worse than no test because it looks like coverage. Fatal paths in
  // this tree are not testable in-process; the predicate behind them is, which is one of the
  // reasons `Check` is its own function.
  TEST_METHOD(RulesThatContradictTheDesignAreRefused)
  {
    Assert::IsTrue(Check(Lockstep::MatchRules{}) == Lockstep::RulesProblem::None, L"the defaults must describe a playable game");

    const auto problemWith = [](auto _break)
    {
      Lockstep::MatchRules rules;
      _break(rules);
      return Check(rules);
    };

    Assert::IsTrue(problemWith([](Lockstep::MatchRules& _r) { _r.playerCount = 5; }) == Lockstep::RulesProblem::PlayerCountOutOfRange);
    Assert::IsTrue(problemWith([](Lockstep::MatchRules& _r) { _r.playerCount = 13; }) == Lockstep::RulesProblem::PlayerCountOutOfRange);
    Assert::IsTrue(problemWith([](Lockstep::MatchRules& _r) { _r.tradeLaneIncome = _r.internalLaneIncome; }) ==
                     Lockstep::RulesProblem::TradeLaneNotWorthBuilding,
                   L"the incentive to talk has to survive the numbers");
    Assert::IsTrue(problemWith([](Lockstep::MatchRules& _r) { _r.siegeTicks = 0; }) == Lockstep::RulesProblem::SiegeIsInstant);
    Assert::IsTrue(problemWith([](Lockstep::MatchRules& _r) { _r.matchLengthTicks = 0; }) == Lockstep::RulesProblem::NoTicksToPlay);
    Assert::IsTrue(problemWith([](Lockstep::MatchRules& _r) { _r.proposalWindowTicks = 0; }) == Lockstep::RulesProblem::NoProposalWindow);
  }

  TEST_METHOD(EveryRulesProblemDescribesItself)
  {
    for (std::uint8_t problem = 0; problem <= static_cast<std::uint8_t>(Lockstep::RulesProblem::NoProposalWindow); ++problem)
    {
      const char* text = Describe(static_cast<Lockstep::RulesProblem>(problem));
      Assert::IsNotNull(text);
      Assert::AreNotEqual("unknown", text);
    }
  }
};

// Step 3's substance: an order that cannot happen is refused, with a reason, before it happens.
TEST_CLASS(OrderValidationTests)
{
public:
  TEST_METHOD(AWellFormedSetIsAccepted)
  {
    const Lockstep::Match match = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(match, 0);

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(match, 0), .destination = NeighborOf(match, capital)});
    orders.builds.push_back(Lockstep::BuildOrder{.system = capital, .kind = Lockstep::BuildKind::MiningStation});

    Assert::IsTrue(match.Validate(orders).empty(), L"a legal set is refused for no reason");
  }

  TEST_METHOD(AnOrderSetFromNobodyIsRefused)
  {
    const Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{99};
    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NoSuchPlayer));

    orders.player = Lockstep::PlayerId{};
    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NoSuchPlayer));
  }

  TEST_METHOD(YouCannotOrderSomebodyElsesFleet)
  {
    const Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(match, 1), .destination = NeighborOf(match, CapitalOf(match, 1))});

    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NotYourFleet));
  }

  TEST_METHOD(AFleetCannotBeGivenTwoOrders)
  {
    const Lockstep::Match match = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(match, 0);
    const Lockstep::FleetId fleet = FleetOf(match, 0);

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = fleet, .destination = NeighborOf(match, capital)});
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = fleet, .destination = capital});

    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::FleetOrderedTwice));
  }

  // A fleet moves one lane per order. The main page's destination picker is lane-constrained for
  // this reason, and this is the rule it is constrained by.
  TEST_METHOD(AFleetCannotJumpToASystemWithNoLane)
  {
    const Lockstep::Match match = SixPlayerMatch();

    // Another player's capital is three ticks away by construction, never one lane.
    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(match, 0), .destination = CapitalOf(match, 3)});

    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NoLaneToDestination));
  }

  TEST_METHOD(HoldingIsALegalOrder)
  {
    const Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(match, 0), .destination = CapitalOf(match, 0)});

    Assert::IsTrue(match.Validate(orders).empty(), L"choosing to stay is a decision, not the absence of one");
  }

  TEST_METHOD(AFleetUnderWayCannotBeRedirected)
  {
    Lockstep::Match match = SixPlayerMatch();
    const Lockstep::FleetId fleet = FleetOf(match, 0);

    Lockstep::MatchFleet& under = match.MutableFleets()[fleet.AsSize()];
    under.movingFrom = under.at;
    under.movingTo = NeighborOf(match, under.at);
    under.at = Lockstep::SystemId{};
    under.ticksRemaining = 2;

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = fleet, .destination = under.movingTo});

    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::FleetInTransit));
  }

  TEST_METHOD(YouCannotBuildOnSomebodyElsesSystem)
  {
    const Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.builds.push_back(Lockstep::BuildOrder{.system = CapitalOf(match, 2), .kind = Lockstep::BuildKind::Shipyard});

    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NotYourSystem));
  }

  TEST_METHOD(ASecondBuildingOfTheSameKindIsRefused)
  {
    Lockstep::Match match = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(match, 0);
    match.MutableSystems()[capital.AsSize()].hasShipyard = true;

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.builds.push_back(Lockstep::BuildOrder{.system = capital, .kind = Lockstep::BuildKind::Shipyard});

    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::AlreadyBuilt));
  }

  // The affordability check runs against a RUNNING total, so a player who can pay for one of two
  // is refused the second and keeps the first.
  TEST_METHOD(TheSecondUnaffordableBuildIsTheOneRefused)
  {
    Lockstep::Match match = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(match, 0);
    const Lockstep::SystemId neighbor = NeighborOf(match, capital);
    match.MutableSystems()[neighbor.AsSize()].owner = Lockstep::PlayerId{0};
    match.MutablePlayers()[0].credits = match.Rules().shipyardCost;

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.builds.push_back(Lockstep::BuildOrder{.system = capital, .kind = Lockstep::BuildKind::Shipyard});
    orders.builds.push_back(Lockstep::BuildOrder{.system = neighbor, .kind = Lockstep::BuildKind::Shipyard});

    const std::vector<Lockstep::RejectedOrder> rejected = match.Validate(orders);
    Assert::AreEqual(static_cast<size_t>(1), rejected.size(), L"exactly one is refused");
    Assert::IsTrue(rejected.front().reason == Lockstep::OrderRejection::CannotAfford);
    Assert::AreEqual(1, rejected.front().index, L"the second one, and the player keeps the first");
  }

  TEST_METHOD(YouCannotProposeToYourselfOrToNobody)
  {
    const Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.proposals.push_back(Lockstep::ProposalOrder{.to = Lockstep::PlayerId{0}, .kind = Lockstep::ProposalKind::ShareScouting});
    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NoSuchRecipient));

    orders.proposals.clear();
    orders.proposals.push_back(Lockstep::ProposalOrder{.to = Lockstep::PlayerId{42}, .kind = Lockstep::ProposalKind::ShareScouting});
    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NoSuchRecipient));
  }

  // "Nobody is ever shown a dead offer as acceptable." A lane between systems the two players do
  // not hold is exactly that.
  TEST_METHOD(ALaneProposalMustJoinTheTwoEmpires)
  {
    const Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.proposals.push_back(
      Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::OpenLane, .lane = Lockstep::LaneId{0}});

    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::LaneNotBetweenYou));
  }

  TEST_METHOD(AHoldWindowOutsideTheProposalWindowIsRefused)
  {
    const Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.proposals.push_back(
      Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::HoldForTicks, .ticks = 0});
    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::BadHoldWindow));

    orders.proposals.clear();
    orders.proposals.push_back(Lockstep::ProposalOrder{
      .to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::HoldForTicks, .ticks = match.Rules().proposalWindowTicks + 1});
    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::BadHoldWindow));
  }

  TEST_METHOD(YouCanOnlyAnswerAnOfferMadeToYou)
  {
    Lockstep::Match match = SixPlayerMatch();

    Lockstep::OpenProposal open;
    open.id = match.TakeNextProposalId();
    open.from = Lockstep::PlayerId{1};
    open.to = Lockstep::PlayerId{2};
    open.kind = Lockstep::ProposalKind::ShareScouting;
    match.MutableProposals().push_back(open);

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.answers.push_back(Lockstep::AnswerOrder{.proposal = open.id, .answer = Lockstep::Answer::Accept});
    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NotYoursToAnswer));

    orders.player = Lockstep::PlayerId{2};
    Assert::IsTrue(match.Validate(orders).empty(), L"the recipient may answer it");
  }

  TEST_METHOD(YouCanOnlyWithdrawAnOfferYouMade)
  {
    Lockstep::Match match = SixPlayerMatch();

    Lockstep::OpenProposal open;
    open.id = match.TakeNextProposalId();
    open.from = Lockstep::PlayerId{1};
    open.to = Lockstep::PlayerId{2};
    match.MutableProposals().push_back(open);

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{2};
    orders.withdrawals.push_back(Lockstep::WithdrawOrder{.proposal = open.id});
    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NotYoursToWithdraw));

    orders.player = Lockstep::PlayerId{1};
    Assert::IsTrue(match.Validate(orders).empty());
  }

  TEST_METHOD(AnswersToProposalsThatAreGoneAreRefused)
  {
    const Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.answers.push_back(Lockstep::AnswerOrder{.proposal = Lockstep::ProposalId{7}});
    Assert::IsTrue(Refused(match.Validate(orders), Lockstep::OrderRejection::NoSuchProposal));
  }

  TEST_METHOD(AConcededPlayerHasNoOrdersLeft)
  {
    Lockstep::Match match = SixPlayerMatch();

    // Conceding is custodianship that cannot be undone, so both halves have to be set -- and
    // `IsConsistent` refuses a state with only one of them.
    match.MutablePlayers()[0].conceded = true;
    match.MutablePlayers()[0].status = Lockstep::PlayerStatus::Custodian;
    match.MutablePlayers()[0].custodianSince = 1;
    Assert::IsTrue(match.IsConsistent());

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(match, 0), .destination = NeighborOf(match, CapitalOf(match, 0))});

    const std::vector<Lockstep::RejectedOrder> rejected = match.Validate(orders);
    Assert::AreEqual(static_cast<size_t>(1), rejected.size(), L"one refusal for the set, not one per order");
    Assert::IsTrue(rejected.front().reason == Lockstep::OrderRejection::AlreadyConceded);
  }

  TEST_METHOD(EveryRejectionDescribesItselfDistinctly)
  {
    constexpr std::array<Lockstep::OrderRejection, 16> ALL = {
      Lockstep::OrderRejection::None,
      Lockstep::OrderRejection::NoSuchPlayer,
      Lockstep::OrderRejection::NotYourFleet,
      Lockstep::OrderRejection::FleetInTransit,
      Lockstep::OrderRejection::NoLaneToDestination,
      Lockstep::OrderRejection::FleetOrderedTwice,
      Lockstep::OrderRejection::NotYourSystem,
      Lockstep::OrderRejection::AlreadyBuilt,
      Lockstep::OrderRejection::CannotAfford,
      Lockstep::OrderRejection::NoSuchRecipient,
      Lockstep::OrderRejection::LaneNotBetweenYou,
      Lockstep::OrderRejection::BadHoldWindow,
      Lockstep::OrderRejection::NoSuchProposal,
      Lockstep::OrderRejection::NotYoursToAnswer,
      Lockstep::OrderRejection::NotYoursToWithdraw,
      Lockstep::OrderRejection::AlreadyConceded,
    };

    std::vector<std::string> seen;
    for (const Lockstep::OrderRejection rejection : ALL)
    {
      const char* text = Lockstep::Describe(rejection);
      Assert::IsNotNull(text);
      Assert::IsTrue(std::find(seen.begin(), seen.end(), text) == seen.end(), L"two rejections share a description");
      seen.emplace_back(text);
    }
  }
};

// An OrderSet is what a client sends and what a replay is made of, so it has to survive the trip.
TEST_CLASS(OrderSerializationTests)
{
public:
  [[nodiscard]] static Lockstep::OrderSet Populated()
  {
    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{3};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = Lockstep::FleetId{1}, .destination = Lockstep::SystemId{17}});
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = Lockstep::FleetId{4}, .destination = Lockstep::SystemId{2}});
    orders.builds.push_back(Lockstep::BuildOrder{.system = Lockstep::SystemId{9}, .kind = Lockstep::BuildKind::MiningStation});
    orders.proposals.push_back(Lockstep::ProposalOrder{
      .to = Lockstep::PlayerId{5}, .kind = Lockstep::ProposalKind::HoldForTicks, .lane = Lockstep::LaneId{12}, .ticks = 3});
    orders.answers.push_back(Lockstep::AnswerOrder{.proposal = Lockstep::ProposalId{2}, .answer = Lockstep::Answer::Decline});
    orders.withdrawals.push_back(Lockstep::WithdrawOrder{.proposal = Lockstep::ProposalId{6}});
    orders.concede = true;
    return orders;
  }

  static void AssertSame(const Lockstep::OrderSet& _expected, const Lockstep::OrderSet& _actual)
  {
    Assert::IsTrue(_expected.player == _actual.player);
    Assert::AreEqual(_expected.fleetOrders.size(), _actual.fleetOrders.size());
    for (std::size_t index = 0; index < _expected.fleetOrders.size(); ++index)
    {
      Assert::IsTrue(_expected.fleetOrders[index].fleet == _actual.fleetOrders[index].fleet);
      Assert::IsTrue(_expected.fleetOrders[index].destination == _actual.fleetOrders[index].destination);
    }
    Assert::AreEqual(_expected.builds.size(), _actual.builds.size());
    for (std::size_t index = 0; index < _expected.builds.size(); ++index)
    {
      Assert::IsTrue(_expected.builds[index].system == _actual.builds[index].system);
      Assert::IsTrue(_expected.builds[index].kind == _actual.builds[index].kind);
    }
    Assert::AreEqual(_expected.proposals.size(), _actual.proposals.size());
    for (std::size_t index = 0; index < _expected.proposals.size(); ++index)
    {
      Assert::IsTrue(_expected.proposals[index].to == _actual.proposals[index].to);
      Assert::IsTrue(_expected.proposals[index].kind == _actual.proposals[index].kind);
      Assert::IsTrue(_expected.proposals[index].lane == _actual.proposals[index].lane);
      Assert::AreEqual(_expected.proposals[index].ticks, _actual.proposals[index].ticks);
    }
    Assert::AreEqual(_expected.answers.size(), _actual.answers.size());
    for (std::size_t index = 0; index < _expected.answers.size(); ++index)
    {
      Assert::IsTrue(_expected.answers[index].proposal == _actual.answers[index].proposal);
      Assert::IsTrue(_expected.answers[index].answer == _actual.answers[index].answer);
    }
    Assert::AreEqual(_expected.withdrawals.size(), _actual.withdrawals.size());
    Assert::AreEqual(_expected.concede, _actual.concede);
  }

  TEST_METHOD(AnOrderSetSurvivesTheRoundTrip)
  {
    const Lockstep::OrderSet original = Populated();

    Neuron::ByteWriter writer;
    original.Write(writer);

    Neuron::ByteReader reader{writer.Bytes()};
    const Lockstep::OrderSet returned = Lockstep::OrderSet::Read(reader);

    Assert::IsFalse(reader.Failed(), L"a record this writer wrote must decode");
    Assert::IsTrue(reader.AtEnd(), L"and must consume exactly what was written");
    AssertSame(original, returned);
  }

  TEST_METHOD(AnEmptySetSurvivesTheRoundTrip)
  {
    const Lockstep::OrderSet original;

    Neuron::ByteWriter writer;
    original.Write(writer);

    Neuron::ByteReader reader{writer.Bytes()};
    const Lockstep::OrderSet returned = Lockstep::OrderSet::Read(reader);

    Assert::IsTrue(reader.AtEnd());
    AssertSame(original, returned);
    Assert::IsFalse(returned.player.IsValid(), L"an unset id comes back unset, not as player zero");
  }

  // The bytes are going to come off a socket one day, and a socket delivers whatever it likes.
  TEST_METHOD(ATruncatedRecordFailsRatherThanLies)
  {
    Neuron::ByteWriter writer;
    Populated().Write(writer);

    for (std::size_t cut : {std::size_t{0}, std::size_t{1}, writer.Size() / 2, writer.Size() - 1})
    {
      const std::span<const std::uint8_t> shortened{writer.Bytes().data(), cut};
      Neuron::ByteReader reader{shortened};
      (void)Lockstep::OrderSet::Read(reader);
      Assert::IsTrue(reader.Failed(), (std::wstring(L"cut at ") + std::to_wstring(cut)).c_str());
    }
  }

  // "Not a chat game -- v1 has no free text." The one-pager lists it under *What it is not*, and an
  // absence is the hardest kind of rule to keep: nothing fails when somebody adds a field.
  //
  // So it is asserted structurally. A fully populated `OrderSet` serialises to a size that can be
  // computed from the counts alone -- ids, enums, counts and flags, all fixed-width. A `std::string`
  // anywhere in an order would make the size depend on what somebody typed, and this arithmetic
  // would stop matching.
  TEST_METHOD(NoOrderCarriesFreeText)
  {
    constexpr std::size_t ID = 4;
    constexpr std::size_t COUNT = 4;
    constexpr std::size_t ENUM = 1;
    constexpr std::size_t FLAG = 1;

    const Lockstep::OrderSet orders = Populated();

    const std::size_t expected = ID                                                                // the player
                                 + COUNT + orders.fleetOrders.size() * (ID + ID)                   // fleet, destination
                                 + COUNT + orders.builds.size() * (ID + ENUM)                      // system, kind
                                 + COUNT + orders.proposals.size() * (ID + ENUM + ID + COUNT + ID) // to, kind, lane, ticks, conditional
                                 + COUNT + orders.answers.size() * (ID + ENUM)                     // proposal, answer
                                 + COUNT + orders.withdrawals.size() * ID                          // proposal
                                 + COUNT + orders.cancellations.size() * ID                        // lane
                                 + FLAG;                                                           // concede

    Neuron::ByteWriter writer;
    orders.Write(writer);

    Assert::AreEqual(expected, writer.Size(), L"an order is ids, enums, counts and flags -- and no prose");
  }

  TEST_METHOD(ALyingLengthDoesNotAllocateTheWorld)
  {
    // A record claiming four billion fleet orders. It must come back empty, not try to hold them.
    Neuron::ByteWriter writer;
    writer.WriteI32(0);
    writer.WriteU32(0xFFFFFFFFU);

    Neuron::ByteReader reader{writer.Bytes()};
    const Lockstep::OrderSet returned = Lockstep::OrderSet::Read(reader);
    Assert::IsTrue(returned.fleetOrders.empty());
  }
};

// Step 4: the six phases, and the rules that live in them.
TEST_CLASS(TickResolutionTests)
{
public:
  TEST_METHOD(ResolvingATickAdvancesItAndLogsSixPhases)
  {
    const Lockstep::Match start = SixPlayerMatch();

    Lockstep::TickLog log;
    const Lockstep::Match after = Lockstep::TickResolver::Resolve(start, {}, log);

    Assert::AreEqual(start.Tick() + 1, after.Tick());
    Assert::AreEqual(0U, log.tick, L"the log is stamped with the tick it resolved");
    Assert::AreEqual(static_cast<size_t>(6), log.phases.size());
    Assert::IsTrue(log.phases[0].phase == Lockstep::Phase::Lock);
    Assert::IsTrue(log.phases[1].phase == Lockstep::Phase::Production);
    Assert::IsTrue(log.phases[2].phase == Lockstep::Phase::Movement);
    Assert::IsTrue(log.phases[3].phase == Lockstep::Phase::Combat);
    Assert::IsTrue(log.phases[4].phase == Lockstep::Phase::Claims);
    Assert::IsTrue(log.phases[5].phase == Lockstep::Phase::Digest);
    Assert::AreEqual(static_cast<size_t>(6), log.digests.size(), L"one digest per player");
    Assert::IsTrue(after.IsConsistent());
  }

  // ADR-018, at the level that matters most: the resolver is a pure function.
  TEST_METHOD(ResolvingTheSameTickTwiceGivesTheSameState)
  {
    const Lockstep::Match start = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(start, 0), .destination = NeighborOf(start, CapitalOf(start, 0))});
    orders.builds.push_back(Lockstep::BuildOrder{.system = CapitalOf(start, 0), .kind = Lockstep::BuildKind::MiningStation});
    const std::array<Lockstep::OrderSet, 1> sets = {orders};

    Assert::AreEqual(Advance(start, sets).Hash(), Advance(start, sets).Hash());
    Assert::AreEqual(start.Hash(), SixPlayerMatch().Hash(), L"and resolving did not touch the state it read");
  }

  TEST_METHOD(TenTicksOfNothingAreReproducible)
  {
    const auto run = []
    {
      Lockstep::Match match = SixPlayerMatch();
      for (std::int32_t tick = 0; tick < 10; ++tick)
      {
        match = AdvanceQuietly(match);
      }
      return match;
    };

    Assert::AreEqual(run().Hash(), run().Hash());
    Assert::AreEqual(10U, run().Tick());
  }

  // ---- Phase 2 -------------------------------------------------------------------------------

  TEST_METHOD(ProductionPaysForSystemsAndInternalLanes)
  {
    const Lockstep::Match start = SixPlayerMatch();
    const Lockstep::Match after = AdvanceQuietly(start);

    // One capital, no other systems, no lanes with both ends held: capital rate only.
    const std::uint32_t expected = start.Rules().startingCredits + start.Rules().creditsPerSystem + start.Rules().capitalCreditsBonus;
    Assert::AreEqual(expected, after.PlayerAt(Lockstep::PlayerId{0}).credits);
  }

  TEST_METHOD(ALaneWithBothEndsHeldPaysItsOwner)
  {
    Lockstep::Match start = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(start, 0);
    const Lockstep::SystemId neighbor = NeighborOf(start, capital);
    start.MutableSystems()[neighbor.AsSize()].owner = Lockstep::PlayerId{0};

    const Lockstep::Match after = AdvanceQuietly(start);

    const std::uint32_t expected = start.Rules().startingCredits + start.Rules().creditsPerSystem + start.Rules().capitalCreditsBonus +
                                   start.Rules().creditsPerSystem + start.Rules().internalLaneIncome;
    Assert::AreEqual(expected, after.PlayerAt(Lockstep::PlayerId{0}).credits);
  }

  // The one-pager makes the trade lane pay more than any internal lane, and that gap is the whole
  // reason to talk to a neighbor.
  TEST_METHOD(ATradeLanePaysBothSidesAndPaysMore)
  {
    Lockstep::Match start = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(start, 0);
    const Lockstep::LaneId lane = start.GalaxyGraph().LanesAt(capital).front();
    const Lockstep::SystemId neighbor = start.GalaxyGraph().OtherEnd(lane, capital);
    start.MutableSystems()[neighbor.AsSize()].owner = Lockstep::PlayerId{1};
    start.MutableTradeLanes().push_back(Lockstep::ActiveTradeLane{.lane = lane, .a = Lockstep::PlayerId{0}, .b = Lockstep::PlayerId{1}});

    const Lockstep::Match after = AdvanceQuietly(start);

    const std::uint32_t before0 = start.PlayerAt(Lockstep::PlayerId{0}).credits;
    const std::uint32_t before1 = start.PlayerAt(Lockstep::PlayerId{1}).credits;
    const std::uint32_t gained0 = after.PlayerAt(Lockstep::PlayerId{0}).credits - before0;
    const std::uint32_t gained1 = after.PlayerAt(Lockstep::PlayerId{1}).credits - before1;

    Assert::IsTrue(gained0 >= start.Rules().tradeLaneIncome, L"the proposer is paid");
    Assert::IsTrue(gained1 >= start.Rules().tradeLaneIncome, L"and so is the partner");
    Assert::IsTrue(start.Rules().tradeLaneIncome > start.Rules().internalLaneIncome);
  }

  TEST_METHOD(AShipyardReinforcesTheFleetStandingOnIt)
  {
    Lockstep::Match start = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(start, 0);
    start.MutableSystems()[capital.AsSize()].hasShipyard = true;

    const std::uint32_t before = start.FleetAt(FleetOf(start, 0)).ships;
    const Lockstep::Match after = AdvanceQuietly(start);

    Assert::AreEqual(before + start.Rules().shipsPerShipyard, after.FleetAt(FleetOf(after, 0)).ships);
  }

  TEST_METHOD(AShipyardWithARivalInTheSystemIsIdle)
  {
    Lockstep::Match start = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(start, 0);
    start.MutableSystems()[capital.AsSize()].hasShipyard = true;

    // A rival fleet parks on it.
    Lockstep::MatchFleet raider;
    raider.owner = Lockstep::PlayerId{1};
    raider.ships = 5;
    raider.at = capital;
    (void)start.AddFleet(raider);

    // The rival is also a battle, so an absolute ship count would be measuring combat. What this
    // test is about is the YARD, so it is measured against the same tick with no yard on it.
    Lockstep::Match withoutYard = start;
    withoutYard.MutableSystems()[capital.AsSize()].hasShipyard = false;

    Lockstep::TickLog log;
    const Lockstep::Match after = Lockstep::TickResolver::Resolve(start, {}, log);
    const Lockstep::Match control = AdvanceQuietly(withoutYard);

    Assert::AreEqual(control.FleetAt(FleetOf(control, 0)).ships, after.FleetAt(FleetOf(after, 0)).ships,
                     L"the yard added nothing that the same tick without one did not");
    Assert::IsTrue(AnyLineContains(log, "is idle"));
  }

  // ---- Phase 3 -------------------------------------------------------------------------------

  TEST_METHOD(AFleetOnAOneTickLaneArrivesThisTick)
  {
    const Lockstep::Match start = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(start, 0);

    // A satellite is one tick from its capital by rule.
    const Lockstep::LaneId lane = start.GalaxyGraph().LanesAt(capital).front();
    Assert::AreEqual(1U, start.GalaxyGraph().LaneAt(lane).costTicks, L"this test needs a one-tick lane");
    const Lockstep::SystemId destination = start.GalaxyGraph().OtherEnd(lane, capital);

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(start, 0), .destination = destination});
    const std::array<Lockstep::OrderSet, 1> sets = {orders};

    const Lockstep::Match after = Advance(start, sets);
    const Lockstep::MatchFleet& fleet = after.FleetAt(FleetOf(after, 0));

    Assert::IsFalse(fleet.InTransit());
    Assert::IsTrue(fleet.at == destination);
    Assert::IsFalse(fleet.orderedTo.IsValid(), L"the order was consumed");
  }

  TEST_METHOD(ALongerLaneTakesItsFullCost)
  {
    Lockstep::Match match = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(match, 0);

    // Find a lane out of the capital that costs more than one.
    Lockstep::LaneId slow;
    for (const Lockstep::LaneId lane : match.GalaxyGraph().LanesAt(capital))
    {
      if (match.GalaxyGraph().LaneAt(lane).costTicks > 1)
      {
        slow = lane;
        break;
      }
    }
    Assert::IsTrue(slow.IsValid(), L"a capital has a lane out of its cluster");

    const std::uint32_t cost = match.GalaxyGraph().LaneAt(slow).costTicks;
    const Lockstep::SystemId destination = match.GalaxyGraph().OtherEnd(slow, capital);

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(match, 0), .destination = destination});
    const std::array<Lockstep::OrderSet, 1> sets = {orders};

    match = Advance(match, sets);
    for (std::uint32_t tick = 1; tick < cost; ++tick)
    {
      Assert::IsTrue(match.FleetAt(FleetOf(match, 0)).InTransit(),
                     (std::wstring(L"still under way after tick ") + std::to_wstring(tick)).c_str());
      match = AdvanceQuietly(match);
    }

    const Lockstep::MatchFleet& arrived = match.FleetAt(FleetOf(match, 0));
    Assert::IsFalse(arrived.InTransit());
    Assert::IsTrue(arrived.at == destination);
  }

  // THE ONE-PAGER'S CENTRAL CONSEQUENCE, and it is testable with combat still a no-op: movement is
  // phase 3 and combat is phase 4, so a fleet ordered out is gone before the fight, and the
  // hostile arriving the same tick finds an empty system.
  TEST_METHOD(AFleetOrderedOutIsGoneBeforeTheHostileArrives)
  {
    Lockstep::Match start = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(start, 0);
    const Lockstep::LaneId lane = start.GalaxyGraph().LanesAt(capital).front();
    const Lockstep::SystemId away = start.GalaxyGraph().OtherEnd(lane, capital);

    // A hostile fleet one tick from arriving at the capital.
    Lockstep::MatchFleet raider;
    raider.owner = Lockstep::PlayerId{1};
    raider.ships = 50;
    raider.movingFrom = away;
    raider.movingTo = capital;
    raider.ticksRemaining = 1;
    const Lockstep::FleetId raiderId = start.AddFleet(raider);

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(start, 0), .destination = away});
    const std::array<Lockstep::OrderSet, 1> sets = {orders};

    const Lockstep::Match after = Advance(start, sets);

    Assert::IsTrue(after.FleetAt(FleetOf(after, 0)).at == away, L"the defender left");
    Assert::IsTrue(after.FleetAt(raiderId).at == capital, L"the raider arrived");
    Assert::IsFalse(after.FleetAt(FleetOf(after, 0)).destroyed, L"and they never met");
  }

  // ---- Phase 5 -------------------------------------------------------------------------------

  TEST_METHOD(AnUncontestedFleetClaimsAnUnclaimedSystem)
  {
    Lockstep::Match start = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(start, 0);
    const Lockstep::SystemId target = NeighborOf(start, capital);
    Assert::IsFalse(start.SystemAt(target).owner.IsValid(), L"this test needs an unowned system");

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = FleetOf(start, 0), .destination = target});
    const std::array<Lockstep::OrderSet, 1> sets = {orders};

    Lockstep::TickLog log;
    const Lockstep::Match after = Lockstep::TickResolver::Resolve(start, {.orders = sets}, log);

    Assert::IsTrue(after.SystemAt(target).owner == Lockstep::PlayerId{0});
    Assert::IsTrue(HasDigestKind(log, 0, Lockstep::DigestKind::SystemClaimed));
  }

  // "Two surviving hostiles: occupied, unclaimed."
  TEST_METHOD(TwoRivalsInOneUnclaimedSystemClaimNothing)
  {
    Lockstep::Match start = SixPlayerMatch();
    const Lockstep::SystemId target = NeighborOf(start, CapitalOf(start, 0));

    for (std::int32_t player = 0; player < 2; ++player)
    {
      Lockstep::MatchFleet fleet;
      fleet.owner = Lockstep::PlayerId{player};
      fleet.ships = 5;
      fleet.at = target;
      (void)start.AddFleet(fleet);
    }

    const Lockstep::Match after = AdvanceQuietly(start);
    Assert::IsFalse(after.SystemAt(target).owner.IsValid(), L"occupied, and still nobody's");
  }

  // Siege, then capture: exactly two consecutive uncontested ticks.
  TEST_METHOD(CaptureTakesTwoConsecutiveUncontestedTicks)
  {
    Lockstep::Match match = SixPlayerMatch();
    match.SetTick(match.Rules().capitalGuardTicks); // past the guard, so a capital can fall

    const Lockstep::SystemId target = CapitalOf(match, 1);
    Lockstep::MatchFleet raider;
    raider.owner = Lockstep::PlayerId{0};
    raider.ships = 50;
    raider.at = target;
    (void)match.AddFleet(raider);

    // The defender's own fleet starts on it, so first move it out of the way.
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = NeighborOf(match, CapitalOf(match, 1));

    Lockstep::TickLog first;
    match = Lockstep::TickResolver::Resolve(match, {}, first);
    Assert::IsTrue(match.SystemAt(target).owner == Lockstep::PlayerId{1}, L"one tick is a siege, not a capture");
    Assert::AreEqual(1U, match.SystemAt(target).siegeTicks);
    Assert::IsTrue(HasDigestKind(first, 1, Lockstep::DigestKind::SiegeBegun));

    Lockstep::TickLog second;
    match = Lockstep::TickResolver::Resolve(match, {}, second);
    Assert::IsTrue(match.SystemAt(target).owner == Lockstep::PlayerId{0}, L"the second consecutive tick takes it");
    Assert::AreEqual(0U, match.SystemAt(target).siegeTicks, L"and the siege is spent");
    Assert::IsTrue(HasDigestKind(second, 1, Lockstep::DigestKind::SystemLost));
    Assert::IsTrue(HasDigestKind(second, 0, Lockstep::DigestKind::SystemClaimed));
  }

  TEST_METHOD(OneContestedTickResetsTheSiege)
  {
    Lockstep::Match match = SixPlayerMatch();
    match.SetTick(match.Rules().capitalGuardTicks);

    const Lockstep::SystemId target = CapitalOf(match, 1);

    // Evenly matched on purpose. With combat live, a besieger that outnumbers the returning owner
    // simply kills it and the siege never breaks -- which is correct, and is not what this test is
    // about. Equal fleets leave both alive, which is what makes the tick contested.
    Lockstep::MatchFleet raider;
    raider.owner = Lockstep::PlayerId{0};
    raider.ships = match.Rules().startingShips;
    raider.at = target;
    const Lockstep::FleetId raiderId = match.AddFleet(raider);
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = NeighborOf(match, CapitalOf(match, 1));

    match = AdvanceQuietly(match);
    Assert::AreEqual(1U, match.SystemAt(target).siegeTicks);

    // The owner comes home. The siege breaks.
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = target;
    match = AdvanceQuietly(match);

    Assert::AreEqual(0U, match.SystemAt(target).siegeTicks, L"one contested tick and it starts again from nothing");
    Assert::IsTrue(match.SystemAt(target).owner == Lockstep::PlayerId{1});

    // And with the owner gone again it is back to one, not two.
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = NeighborOf(match, CapitalOf(match, 1));
    match = AdvanceQuietly(match);
    Assert::AreEqual(1U, match.SystemAt(target).siegeTicks);
    Assert::IsTrue(match.SystemAt(target).owner == Lockstep::PlayerId{1});
    Assert::IsTrue(match.FleetAt(raiderId).at == target);
  }

  // The capital guard is an explicit rule layered on top of the siege rule, not derived from it.
  TEST_METHOD(AGuardedCapitalCannotBeBesieged)
  {
    Lockstep::Match match = SixPlayerMatch();
    const Lockstep::SystemId target = CapitalOf(match, 1);

    Lockstep::MatchFleet raider;
    raider.owner = Lockstep::PlayerId{0};
    raider.ships = 50;
    raider.at = target;
    (void)match.AddFleet(raider);
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = NeighborOf(match, CapitalOf(match, 1));

    for (std::uint32_t tick = 0; tick < match.Rules().capitalGuardTicks; ++tick)
    {
      match = AdvanceQuietly(match);
      Assert::IsTrue(match.SystemAt(target).owner == Lockstep::PlayerId{1},
                     (std::wstring(L"still held at tick ") + std::to_wstring(match.Tick())).c_str());
      Assert::AreEqual(0U, match.SystemAt(target).siegeTicks, L"the guard stops the clock, it does not pause it");
    }

    // The tick the guard lapses, the siege can finally begin.
    match = AdvanceQuietly(match);
    Assert::AreEqual(1U, match.SystemAt(target).siegeTicks);
  }

  TEST_METHOD(TheSealedRegionCannotBeClaimed)
  {
    Lockstep::Match match = SixPlayerMatch();
    const Lockstep::SystemId region = match.GalaxyGraph().RegionAnchor();

    Lockstep::MatchFleet raider;
    raider.owner = Lockstep::PlayerId{0};
    raider.ships = 50;
    raider.at = region;
    (void)match.AddFleet(raider);

    for (std::int32_t tick = 0; tick < 4; ++tick)
    {
      match = AdvanceQuietly(match);
    }

    Assert::IsFalse(match.SystemAt(region).owner.IsValid(), L"empires can raid it, not claim it");
  }

  // ---- Phase 1, and the diplomacy the one-pager builds on it ---------------------------------

  TEST_METHOD(AProposalArrivesInTheRecipientsDigest)
  {
    const Lockstep::Match start = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.proposals.push_back(Lockstep::ProposalOrder{.to = Lockstep::PlayerId{2}, .kind = Lockstep::ProposalKind::ShareScouting});
    const std::array<Lockstep::OrderSet, 1> sets = {orders};

    Lockstep::TickLog log;
    const Lockstep::Match after = Lockstep::TickResolver::Resolve(start, {.orders = sets}, log);

    Assert::AreEqual(static_cast<size_t>(1), after.Proposals().size());
    Assert::IsTrue(HasDigestKind(log, 2, Lockstep::DigestKind::ProposalReceived));
    Assert::IsFalse(HasDigestKind(log, 3, Lockstep::DigestKind::ProposalReceived), L"and in nobody else's");
  }

  TEST_METHOD(AcceptingALaneOpensItAndChargesTheProposer)
  {
    Lockstep::Match match = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(match, 0);
    const Lockstep::LaneId lane = match.GalaxyGraph().LanesAt(capital).front();
    const Lockstep::SystemId neighbor = match.GalaxyGraph().OtherEnd(lane, capital);
    match.MutableSystems()[neighbor.AsSize()].owner = Lockstep::PlayerId{1};

    Lockstep::OrderSet propose;
    propose.player = Lockstep::PlayerId{0};
    propose.proposals.push_back(
      Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::OpenLane, .lane = lane});
    const std::array<Lockstep::OrderSet, 1> proposing = {propose};
    match = Advance(match, proposing);

    Assert::AreEqual(static_cast<size_t>(1), match.Proposals().size());
    const Lockstep::ProposalId offer = match.Proposals().front().id;
    const std::uint32_t purseBefore = match.PlayerAt(Lockstep::PlayerId{0}).credits;

    Lockstep::OrderSet accept;
    accept.player = Lockstep::PlayerId{1};
    accept.answers.push_back(Lockstep::AnswerOrder{.proposal = offer, .answer = Lockstep::Answer::Accept});
    const std::array<Lockstep::OrderSet, 1> accepting = {accept};

    Lockstep::TickLog log;
    match = Lockstep::TickResolver::Resolve(match, {.orders = accepting}, log);

    Assert::AreEqual(static_cast<size_t>(1), match.TradeLanes().size(), L"the lane is open");
    Assert::IsTrue(match.Proposals().empty(), L"and the offer is off the table");
    Assert::IsTrue(HasDigestKind(log, 0, Lockstep::DigestKind::LaneOpened));
    Assert::IsTrue(HasDigestKind(log, 1, Lockstep::DigestKind::LaneOpened));

    // Charged the proposer, then paid both. The net has to be the cost less this tick's income.
    Assert::IsTrue(match.PlayerAt(Lockstep::PlayerId{0}).credits <=
                     purseBefore + match.Rules().tradeLaneIncome + match.Rules().creditsPerSystem + match.Rules().capitalCreditsBonus,
                   L"the lane was paid for");
  }

  TEST_METHOD(AnUnansweredProposalIsReportedAsIgnored)
  {
    Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet propose;
    propose.player = Lockstep::PlayerId{0};
    propose.proposals.push_back(Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::ShareScouting});
    const std::array<Lockstep::OrderSet, 1> proposing = {propose};
    match = Advance(match, proposing);

    bool reported = false;
    for (std::uint32_t tick = 0; tick <= match.Rules().proposalWindowTicks; ++tick)
    {
      Lockstep::TickLog log;
      match = Lockstep::TickResolver::Resolve(match, {}, log);
      if (HasDigestKind(log, 0, Lockstep::DigestKind::ProposalIgnored))
      {
        reported = true;
        break;
      }
    }

    Assert::IsTrue(reported, L"the proposer is told it was ignored");
    Assert::IsTrue(match.Proposals().empty(), L"and it is off the table");
  }

  TEST_METHOD(AWithdrawnProposalTellsTheRecipient)
  {
    Lockstep::Match match = SixPlayerMatch();

    Lockstep::OrderSet propose;
    propose.player = Lockstep::PlayerId{0};
    propose.proposals.push_back(Lockstep::ProposalOrder{.to = Lockstep::PlayerId{1}, .kind = Lockstep::ProposalKind::ShareScouting});
    const std::array<Lockstep::OrderSet, 1> proposing = {propose};
    match = Advance(match, proposing);

    Lockstep::OrderSet withdraw;
    withdraw.player = Lockstep::PlayerId{0};
    withdraw.withdrawals.push_back(Lockstep::WithdrawOrder{.proposal = match.Proposals().front().id});
    const std::array<Lockstep::OrderSet, 1> withdrawing = {withdraw};

    Lockstep::TickLog log;
    match = Lockstep::TickResolver::Resolve(match, {.orders = withdrawing}, log);

    Assert::IsTrue(match.Proposals().empty());
    Assert::IsTrue(HasDigestKind(log, 1, Lockstep::DigestKind::ProposalWithdrawn));
  }

  // "Canceled by partner" and "canceled: system lost" are different sentences, and the one-pager
  // is explicit that the digest must distinguish them.
  TEST_METHOD(ATradeLaneCancelsWhenAnEndpointChangesHands)
  {
    Lockstep::Match match = SixPlayerMatch();
    match.SetTick(match.Rules().capitalGuardTicks);

    const Lockstep::SystemId capital = CapitalOf(match, 0);
    const Lockstep::LaneId lane = match.GalaxyGraph().LanesAt(capital).front();
    const Lockstep::SystemId neighbor = match.GalaxyGraph().OtherEnd(lane, capital);

    match.MutableSystems()[neighbor.AsSize()].owner = Lockstep::PlayerId{1};
    match.MutableTradeLanes().push_back(Lockstep::ActiveTradeLane{.lane = lane, .a = Lockstep::PlayerId{0}, .b = Lockstep::PlayerId{1}});

    // Player 2 takes the neighbor: two ticks of siege on an unguarded, undefended system.
    Lockstep::MatchFleet raider;
    raider.owner = Lockstep::PlayerId{2};
    raider.ships = 50;
    raider.at = neighbor;
    (void)match.AddFleet(raider);

    Lockstep::TickLog log;
    for (std::int32_t tick = 0; tick < 2; ++tick)
    {
      match = Lockstep::TickResolver::Resolve(match, {}, log);
    }

    Assert::IsTrue(match.SystemAt(neighbor).owner == Lockstep::PlayerId{2}, L"the endpoint changed hands");
    Assert::IsTrue(match.TradeLanes().empty(), L"so the lane is gone");
    Assert::IsTrue(HasDigestKind(log, 0, Lockstep::DigestKind::LaneCanceled));
    Assert::IsTrue(HasDigestKind(log, 1, Lockstep::DigestKind::LaneCanceled));
  }

  TEST_METHOD(ARefusedOrderReachesTheDigestWithItsReason)
  {
    const Lockstep::Match start = SixPlayerMatch();

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.builds.push_back(Lockstep::BuildOrder{.system = CapitalOf(start, 4), .kind = Lockstep::BuildKind::Shipyard});
    const std::array<Lockstep::OrderSet, 1> sets = {orders};

    Lockstep::TickLog log;
    const Lockstep::Match after = Lockstep::TickResolver::Resolve(start, {.orders = sets}, log);

    Assert::IsTrue(HasDigestKind(log, 0, Lockstep::DigestKind::OrderRejected), L"nothing is dropped silently");
    Assert::IsFalse(after.SystemAt(CapitalOf(after, 4)).hasShipyard, L"and nothing was built");
    Assert::AreEqual(start.PlayerAt(Lockstep::PlayerId{0}).credits + after.Rules().creditsPerSystem + after.Rules().capitalCreditsBonus,
                     after.PlayerAt(Lockstep::PlayerId{0}).credits, L"nor paid for");
  }

  TEST_METHOD(ASecondOrderSetFromOnePlayerIsDiscarded)
  {
    const Lockstep::Match start = SixPlayerMatch();
    const Lockstep::SystemId capital = CapitalOf(start, 0);

    Lockstep::OrderSet once;
    once.player = Lockstep::PlayerId{0};
    once.builds.push_back(Lockstep::BuildOrder{.system = capital, .kind = Lockstep::BuildKind::MiningStation});

    const std::array<Lockstep::OrderSet, 2> twice = {once, once};

    Lockstep::TickLog log;
    const Lockstep::Match after = Lockstep::TickResolver::Resolve(start, {.orders = twice}, log);

    Assert::IsTrue(after.SystemAt(capital).hasMiningStation);
    Assert::AreEqual(start.PlayerAt(Lockstep::PlayerId{0}).credits - start.Rules().miningStationCost + after.Rules().creditsPerSystem +
                       after.Rules().capitalCreditsBonus + after.Rules().miningStationCredits,
                     after.PlayerAt(Lockstep::PlayerId{0}).credits, L"charged once, not twice");
    Assert::IsTrue(AnyLineContains(log, "submitted twice"));
  }

  // ---- Phase 6 -------------------------------------------------------------------------------

  // ADR-020: sorted by the severity each event carries, most consequential first.
  TEST_METHOD(TheDigestIsSortedByConsequence)
  {
    Lockstep::Match match = SixPlayerMatch();
    match.SetTick(match.Rules().capitalGuardTicks);

    // Give player 1 a losing tick and a refused order in the same breath: economy is always there,
    // and losing a system must come out on top of both.
    const Lockstep::SystemId target = CapitalOf(match, 1);
    match.MutableSystems()[target.AsSize()].siegeBy = Lockstep::PlayerId{0};
    match.MutableSystems()[target.AsSize()].siegeTicks = 1;
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = NeighborOf(match, CapitalOf(match, 1));

    Lockstep::MatchFleet raider;
    raider.owner = Lockstep::PlayerId{0};
    raider.ships = 50;
    raider.at = target;
    (void)match.AddFleet(raider);

    Lockstep::OrderSet bad;
    bad.player = Lockstep::PlayerId{1};
    bad.builds.push_back(Lockstep::BuildOrder{.system = CapitalOf(match, 3), .kind = Lockstep::BuildKind::Shipyard});
    const std::array<Lockstep::OrderSet, 1> sets = {bad};

    Lockstep::TickLog log;
    match = Lockstep::TickResolver::Resolve(match, {.orders = sets}, log);

    const std::vector<Lockstep::DigestEntry>& digest = log.digests[1];
    Assert::IsTrue(digest.size() >= 3, L"a loss, a refusal and the economy line");
    Assert::IsTrue(digest.front().kind == Lockstep::DigestKind::SystemLost, L"the top event is the one that changes what you do");

    for (std::size_t index = 1; index < digest.size(); ++index)
    {
      Assert::IsTrue(digest[index - 1].severity >= digest[index].severity, L"sorted descending, with no exceptions");
    }
  }

  TEST_METHOD(EveryPhaseAndDigestKindDescribesItself)
  {
    constexpr std::array<Lockstep::Phase, 6> PHASES = {Lockstep::Phase::Lock,   Lockstep::Phase::Production, Lockstep::Phase::Movement,
                                                       Lockstep::Phase::Combat, Lockstep::Phase::Claims,     Lockstep::Phase::Digest};
    std::vector<std::string> seen;
    for (const Lockstep::Phase phase : PHASES)
    {
      const char* text = Describe(phase);
      Assert::IsNotNull(text);
      Assert::IsTrue(std::find(seen.begin(), seen.end(), text) == seen.end());
      seen.emplace_back(text);
    }

    // Every DigestKind, by walking the range rather than listing it -- a new kind added without a
    // description should fail here rather than print "unknown" to a player.
    for (std::uint8_t kind = 0; kind <= static_cast<std::uint8_t>(Lockstep::DigestKind::Custodian); ++kind)
    {
      const char* text = Describe(static_cast<Lockstep::DigestKind>(kind));
      Assert::IsNotNull(text);
      Assert::AreNotEqual("unknown", text);
    }
  }

  // The phase is in the log whether or not anything happened in it, which is what lets a replay
  // written before step 5 and one written after have the same six entries.
  TEST_METHOD(ThePeacefulTickHasACombatPhaseWithNoBattlesInIt)
  {
    Lockstep::TickLog log;
    (void)Lockstep::TickResolver::Resolve(SixPlayerMatch(), {}, log);

    const Lockstep::PhaseRecord* combat = log.Find(Lockstep::Phase::Combat);
    Assert::IsNotNull(combat);
    Assert::IsTrue(combat->lines.empty(), L"nobody was next to anybody yet");
  }
};

} // namespace GameLogicTests
