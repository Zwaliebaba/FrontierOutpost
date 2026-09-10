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

[[nodiscard]] Frontier::Match SixPlayerMatch()
{
  Frontier::MatchRules rules;
  rules.playerCount = 6;
  return Frontier::Match::Create(rules, SEED);
}

/// The capital of player n, which is where its starting fleet is.
[[nodiscard]] Frontier::SystemId CapitalOf(const Frontier::Match& _match, std::int32_t _player)
{
  return _match.GalaxyGraph().Capitals()[static_cast<std::size_t>(_player)];
}

/// A system one lane from `_from`, and the lane's cost.
[[nodiscard]] Frontier::SystemId NeighborOf(const Frontier::Match& _match, Frontier::SystemId _from)
{
  const Frontier::LaneId lane = _match.GalaxyGraph().LanesAt(_from).front();
  return _match.GalaxyGraph().OtherEnd(lane, _from);
}

[[nodiscard]] Frontier::FleetId FleetOf(const Frontier::Match& _match, std::int32_t _player)
{
  for (std::size_t index = 0; index < _match.Fleets().size(); ++index)
  {
    if (_match.Fleets()[index].owner == Frontier::PlayerId{_player} && !_match.Fleets()[index].destroyed)
    {
      return Frontier::FleetId{static_cast<std::int32_t>(index)};
    }
  }
  return Frontier::FleetId{};
}

/// Resolves one tick with the given order sets and throws the log away.
[[nodiscard]] Frontier::Match Advance(const Frontier::Match& _match, std::span<const Frontier::OrderSet> _orders)
{
  Frontier::TickLog log;
  return Frontier::TickResolver::Resolve(_match, _orders, log);
}

[[nodiscard]] Frontier::Match AdvanceQuietly(const Frontier::Match& _match)
{
  return Advance(_match, {});
}

/// Every reason in a rejection list, so a test can say what it expected rather than an index.
[[nodiscard]] bool Refused(const std::vector<Frontier::RejectedOrder>& _rejected, Frontier::OrderRejection _reason)
{
  return std::any_of(_rejected.begin(), _rejected.end(),
                     [_reason](const Frontier::RejectedOrder& _refusal) { return _refusal.reason == _reason; });
}

[[nodiscard]] bool AnyLineContains(const Frontier::TickLog& _log, std::string_view _fragment)
{
  const std::vector<std::string> lines = _log.AllLines();
  return std::any_of(lines.begin(), lines.end(),
                     [_fragment](const std::string& _line) { return _line.find(_fragment) != std::string::npos; });
}

[[nodiscard]] bool HasDigestKind(const Frontier::TickLog& _log, std::int32_t _player, Frontier::DigestKind _kind)
{
  const std::vector<Frontier::DigestEntry>& digest = _log.digests[static_cast<std::size_t>(_player)];
  return std::any_of(digest.begin(), digest.end(), [_kind](const Frontier::DigestEntry& _entry) { return _entry.kind == _kind; });
}

} // namespace

// A match is a value: created from rules and a seed, hashable, and self-consistent.
TEST_CLASS(MatchStateTests)
{
public:
  TEST_METHOD(EveryPlayerStartsOnACapitalWithAFleet)
  {
    const Frontier::Match match = SixPlayerMatch();

    Assert::AreEqual(static_cast<size_t>(6), match.Players().size());
    Assert::AreEqual(static_cast<size_t>(6), match.Fleets().size(), L"one starting fleet each");
    Assert::AreEqual(0U, match.Tick());
    Assert::IsTrue(match.IsConsistent());

    for (std::int32_t player = 0; player < 6; ++player)
    {
      const Frontier::SystemId capital = CapitalOf(match, player);
      Assert::IsTrue(match.SystemAt(capital).owner == Frontier::PlayerId{player}, L"a capital starts held by its player");
      Assert::AreEqual(match.Rules().startingCredits, match.PlayerAt(Frontier::PlayerId{player}).credits);

      const Frontier::FleetId fleet = FleetOf(match, player);
      Assert::IsTrue(fleet.IsValid());
      Assert::IsTrue(match.FleetAt(fleet).at == capital, L"the fleet starts on the capital");
      Assert::IsFalse(match.FleetAt(fleet).InTransit());
    }
  }

  TEST_METHOD(NothingButCapitalsIsOwnedAtTickZero)
  {
    const Frontier::Match match = SixPlayerMatch();

    std::uint32_t owned = 0;
    for (const Frontier::SystemState& system : match.Systems())
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

    Frontier::MatchRules other;
    other.playerCount = 6;
    Assert::AreNotEqual(SixPlayerMatch().Hash(), Frontier::Match::Create(other, SEED + 1).Hash(), L"a different seed is a different match");
  }

  // Every field a rule reads has to be in the hash, or it can drift without a test noticing. These
  // check the ones a resolver phase writes.
  TEST_METHOD(TheHashCoversWhatTheResolverWrites)
  {
    const Frontier::Match base = SixPlayerMatch();

    const auto changed = [&base](auto _mutate)
    {
      Frontier::Match copy = base;
      _mutate(copy);
      return copy.Hash() != base.Hash();
    };

    Assert::IsTrue(changed([](Frontier::Match& _m) { _m.MutablePlayers()[0].credits += 1; }), L"credits");
    Assert::IsTrue(changed([](Frontier::Match& _m) { _m.MutablePlayers()[0].conceded = true; }), L"concession");
    Assert::IsTrue(changed([](Frontier::Match& _m) { _m.MutableSystems()[1].hasShipyard = true; }), L"buildings");
    Assert::IsTrue(changed([](Frontier::Match& _m) { _m.MutableSystems()[1].siegeTicks = 1; }), L"siege progress");
    Assert::IsTrue(changed([](Frontier::Match& _m) { _m.MutableFleets()[0].ships += 1; }), L"fleet strength");
    Assert::IsTrue(changed([](Frontier::Match& _m) { _m.MutableFleets()[0].orderedTo = Frontier::SystemId{3}; }), L"pending orders");
    Assert::IsTrue(changed([](Frontier::Match& _m) { _m.SetTick(5); }), L"the tick");
  }

  TEST_METHOD(ACapitalIsGuardedForTheFirstTwelveTicks)
  {
    Frontier::Match match = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(match, 0);

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
    Assert::IsTrue(Check(Frontier::MatchRules{}) == Frontier::RulesProblem::None, L"the defaults must describe a playable game");

    const auto problemWith = [](auto _break)
    {
      Frontier::MatchRules rules;
      _break(rules);
      return Check(rules);
    };

    Assert::IsTrue(problemWith([](Frontier::MatchRules& _r) { _r.playerCount = 5; }) == Frontier::RulesProblem::PlayerCountOutOfRange);
    Assert::IsTrue(problemWith([](Frontier::MatchRules& _r) { _r.playerCount = 13; }) == Frontier::RulesProblem::PlayerCountOutOfRange);
    Assert::IsTrue(problemWith([](Frontier::MatchRules& _r) { _r.tradeLaneIncome = _r.internalLaneIncome; }) ==
                     Frontier::RulesProblem::TradeLaneNotWorthBuilding,
                   L"the incentive to talk has to survive the numbers");
    Assert::IsTrue(problemWith([](Frontier::MatchRules& _r) { _r.siegeTicks = 0; }) == Frontier::RulesProblem::SiegeIsInstant);
    Assert::IsTrue(problemWith([](Frontier::MatchRules& _r) { _r.matchLengthTicks = 0; }) == Frontier::RulesProblem::NoTicksToPlay);
    Assert::IsTrue(problemWith([](Frontier::MatchRules& _r) { _r.proposalWindowTicks = 0; }) == Frontier::RulesProblem::NoProposalWindow);
  }

  TEST_METHOD(EveryRulesProblemDescribesItself)
  {
    for (std::uint8_t problem = 0; problem <= static_cast<std::uint8_t>(Frontier::RulesProblem::NoProposalWindow); ++problem)
    {
      const char* text = Describe(static_cast<Frontier::RulesProblem>(problem));
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
    const Frontier::Match match = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(match, 0);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(match, 0), .destination = NeighborOf(match, capital)});
    orders.builds.push_back(Frontier::BuildOrder{.system = capital, .kind = Frontier::BuildKind::MiningStation});

    Assert::IsTrue(match.Validate(orders).empty(), L"a legal set is refused for no reason");
  }

  TEST_METHOD(AnOrderSetFromNobodyIsRefused)
  {
    const Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{99};
    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NoSuchPlayer));

    orders.player = Frontier::PlayerId{};
    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NoSuchPlayer));
  }

  TEST_METHOD(YouCannotOrderSomebodyElsesFleet)
  {
    const Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(match, 1), .destination = NeighborOf(match, CapitalOf(match, 1))});

    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NotYourFleet));
  }

  TEST_METHOD(AFleetCannotBeGivenTwoOrders)
  {
    const Frontier::Match match = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(match, 0);
    const Frontier::FleetId fleet = FleetOf(match, 0);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = fleet, .destination = NeighborOf(match, capital)});
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = fleet, .destination = capital});

    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::FleetOrderedTwice));
  }

  // A fleet moves one lane per order. The main page's destination picker is lane-constrained for
  // this reason, and this is the rule it is constrained by.
  TEST_METHOD(AFleetCannotJumpToASystemWithNoLane)
  {
    const Frontier::Match match = SixPlayerMatch();

    // Another player's capital is three ticks away by construction, never one lane.
    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(match, 0), .destination = CapitalOf(match, 3)});

    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NoLaneToDestination));
  }

  TEST_METHOD(HoldingIsALegalOrder)
  {
    const Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(match, 0), .destination = CapitalOf(match, 0)});

    Assert::IsTrue(match.Validate(orders).empty(), L"choosing to stay is a decision, not the absence of one");
  }

  TEST_METHOD(AFleetUnderWayCannotBeRedirected)
  {
    Frontier::Match match = SixPlayerMatch();
    const Frontier::FleetId fleet = FleetOf(match, 0);

    Frontier::MatchFleet& under = match.MutableFleets()[fleet.AsSize()];
    under.movingFrom = under.at;
    under.movingTo = NeighborOf(match, under.at);
    under.at = Frontier::SystemId{};
    under.ticksRemaining = 2;

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = fleet, .destination = under.movingTo});

    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::FleetInTransit));
  }

  TEST_METHOD(YouCannotBuildOnSomebodyElsesSystem)
  {
    const Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.builds.push_back(Frontier::BuildOrder{.system = CapitalOf(match, 2), .kind = Frontier::BuildKind::Shipyard});

    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NotYourSystem));
  }

  TEST_METHOD(ASecondBuildingOfTheSameKindIsRefused)
  {
    Frontier::Match match = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(match, 0);
    match.MutableSystems()[capital.AsSize()].hasShipyard = true;

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.builds.push_back(Frontier::BuildOrder{.system = capital, .kind = Frontier::BuildKind::Shipyard});

    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::AlreadyBuilt));
  }

  // The affordability check runs against a RUNNING total, so a player who can pay for one of two
  // is refused the second and keeps the first.
  TEST_METHOD(TheSecondUnaffordableBuildIsTheOneRefused)
  {
    Frontier::Match match = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(match, 0);
    const Frontier::SystemId neighbor = NeighborOf(match, capital);
    match.MutableSystems()[neighbor.AsSize()].owner = Frontier::PlayerId{0};
    match.MutablePlayers()[0].credits = match.Rules().shipyardCost;

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.builds.push_back(Frontier::BuildOrder{.system = capital, .kind = Frontier::BuildKind::Shipyard});
    orders.builds.push_back(Frontier::BuildOrder{.system = neighbor, .kind = Frontier::BuildKind::Shipyard});

    const std::vector<Frontier::RejectedOrder> rejected = match.Validate(orders);
    Assert::AreEqual(static_cast<size_t>(1), rejected.size(), L"exactly one is refused");
    Assert::IsTrue(rejected.front().reason == Frontier::OrderRejection::CannotAfford);
    Assert::AreEqual(1, rejected.front().index, L"the second one, and the player keeps the first");
  }

  TEST_METHOD(YouCannotProposeToYourselfOrToNobody)
  {
    const Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.proposals.push_back(Frontier::ProposalOrder{.to = Frontier::PlayerId{0}, .kind = Frontier::ProposalKind::ShareScouting});
    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NoSuchRecipient));

    orders.proposals.clear();
    orders.proposals.push_back(Frontier::ProposalOrder{.to = Frontier::PlayerId{42}, .kind = Frontier::ProposalKind::ShareScouting});
    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NoSuchRecipient));
  }

  // "Nobody is ever shown a dead offer as acceptable." A lane between systems the two players do
  // not hold is exactly that.
  TEST_METHOD(ALaneProposalMustJoinTheTwoEmpires)
  {
    const Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.proposals.push_back(
      Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::OpenLane, .lane = Frontier::LaneId{0}});

    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::LaneNotBetweenYou));
  }

  TEST_METHOD(AHoldWindowOutsideTheProposalWindowIsRefused)
  {
    const Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.proposals.push_back(
      Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::HoldForTicks, .ticks = 0});
    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::BadHoldWindow));

    orders.proposals.clear();
    orders.proposals.push_back(Frontier::ProposalOrder{
      .to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::HoldForTicks, .ticks = match.Rules().proposalWindowTicks + 1});
    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::BadHoldWindow));
  }

  TEST_METHOD(YouCanOnlyAnswerAnOfferMadeToYou)
  {
    Frontier::Match match = SixPlayerMatch();

    Frontier::OpenProposal open;
    open.id = match.TakeNextProposalId();
    open.from = Frontier::PlayerId{1};
    open.to = Frontier::PlayerId{2};
    open.kind = Frontier::ProposalKind::ShareScouting;
    match.MutableProposals().push_back(open);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.answers.push_back(Frontier::AnswerOrder{.proposal = open.id, .answer = Frontier::Answer::Accept});
    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NotYoursToAnswer));

    orders.player = Frontier::PlayerId{2};
    Assert::IsTrue(match.Validate(orders).empty(), L"the recipient may answer it");
  }

  TEST_METHOD(YouCanOnlyWithdrawAnOfferYouMade)
  {
    Frontier::Match match = SixPlayerMatch();

    Frontier::OpenProposal open;
    open.id = match.TakeNextProposalId();
    open.from = Frontier::PlayerId{1};
    open.to = Frontier::PlayerId{2};
    match.MutableProposals().push_back(open);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{2};
    orders.withdrawals.push_back(Frontier::WithdrawOrder{.proposal = open.id});
    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NotYoursToWithdraw));

    orders.player = Frontier::PlayerId{1};
    Assert::IsTrue(match.Validate(orders).empty());
  }

  TEST_METHOD(AnswersToProposalsThatAreGoneAreRefused)
  {
    const Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.answers.push_back(Frontier::AnswerOrder{.proposal = Frontier::ProposalId{7}});
    Assert::IsTrue(Refused(match.Validate(orders), Frontier::OrderRejection::NoSuchProposal));
  }

  TEST_METHOD(AConcededPlayerHasNoOrdersLeft)
  {
    Frontier::Match match = SixPlayerMatch();
    match.MutablePlayers()[0].conceded = true;

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(match, 0), .destination = NeighborOf(match, CapitalOf(match, 0))});

    const std::vector<Frontier::RejectedOrder> rejected = match.Validate(orders);
    Assert::AreEqual(static_cast<size_t>(1), rejected.size(), L"one refusal for the set, not one per order");
    Assert::IsTrue(rejected.front().reason == Frontier::OrderRejection::AlreadyConceded);
  }

  TEST_METHOD(EveryRejectionDescribesItselfDistinctly)
  {
    constexpr std::array<Frontier::OrderRejection, 16> ALL = {
      Frontier::OrderRejection::None,
      Frontier::OrderRejection::NoSuchPlayer,
      Frontier::OrderRejection::NotYourFleet,
      Frontier::OrderRejection::FleetInTransit,
      Frontier::OrderRejection::NoLaneToDestination,
      Frontier::OrderRejection::FleetOrderedTwice,
      Frontier::OrderRejection::NotYourSystem,
      Frontier::OrderRejection::AlreadyBuilt,
      Frontier::OrderRejection::CannotAfford,
      Frontier::OrderRejection::NoSuchRecipient,
      Frontier::OrderRejection::LaneNotBetweenYou,
      Frontier::OrderRejection::BadHoldWindow,
      Frontier::OrderRejection::NoSuchProposal,
      Frontier::OrderRejection::NotYoursToAnswer,
      Frontier::OrderRejection::NotYoursToWithdraw,
      Frontier::OrderRejection::AlreadyConceded,
    };

    std::vector<std::string> seen;
    for (const Frontier::OrderRejection rejection : ALL)
    {
      const char* text = Frontier::Describe(rejection);
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
  [[nodiscard]] static Frontier::OrderSet Populated()
  {
    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{3};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = Frontier::FleetId{1}, .destination = Frontier::SystemId{17}});
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = Frontier::FleetId{4}, .destination = Frontier::SystemId{2}});
    orders.builds.push_back(Frontier::BuildOrder{.system = Frontier::SystemId{9}, .kind = Frontier::BuildKind::MiningStation});
    orders.proposals.push_back(Frontier::ProposalOrder{
      .to = Frontier::PlayerId{5}, .kind = Frontier::ProposalKind::HoldForTicks, .lane = Frontier::LaneId{12}, .ticks = 3});
    orders.answers.push_back(Frontier::AnswerOrder{.proposal = Frontier::ProposalId{2}, .answer = Frontier::Answer::Decline});
    orders.withdrawals.push_back(Frontier::WithdrawOrder{.proposal = Frontier::ProposalId{6}});
    orders.concede = true;
    return orders;
  }

  static void AssertSame(const Frontier::OrderSet& _expected, const Frontier::OrderSet& _actual)
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
    const Frontier::OrderSet original = Populated();

    Neuron::ByteWriter writer;
    original.Write(writer);

    Neuron::ByteReader reader{writer.Bytes()};
    const Frontier::OrderSet returned = Frontier::OrderSet::Read(reader);

    Assert::IsFalse(reader.Failed(), L"a record this writer wrote must decode");
    Assert::IsTrue(reader.AtEnd(), L"and must consume exactly what was written");
    AssertSame(original, returned);
  }

  TEST_METHOD(AnEmptySetSurvivesTheRoundTrip)
  {
    const Frontier::OrderSet original;

    Neuron::ByteWriter writer;
    original.Write(writer);

    Neuron::ByteReader reader{writer.Bytes()};
    const Frontier::OrderSet returned = Frontier::OrderSet::Read(reader);

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
      (void)Frontier::OrderSet::Read(reader);
      Assert::IsTrue(reader.Failed(), (std::wstring(L"cut at ") + std::to_wstring(cut)).c_str());
    }
  }

  TEST_METHOD(ALyingLengthDoesNotAllocateTheWorld)
  {
    // A record claiming four billion fleet orders. It must come back empty, not try to hold them.
    Neuron::ByteWriter writer;
    writer.WriteI32(0);
    writer.WriteU32(0xFFFFFFFFU);

    Neuron::ByteReader reader{writer.Bytes()};
    const Frontier::OrderSet returned = Frontier::OrderSet::Read(reader);
    Assert::IsTrue(returned.fleetOrders.empty());
  }
};

// Step 4: the six phases, and the rules that live in them.
TEST_CLASS(TickResolutionTests)
{
public:
  TEST_METHOD(ResolvingATickAdvancesItAndLogsSixPhases)
  {
    const Frontier::Match start = SixPlayerMatch();

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(start, {}, log);

    Assert::AreEqual(start.Tick() + 1, after.Tick());
    Assert::AreEqual(0U, log.tick, L"the log is stamped with the tick it resolved");
    Assert::AreEqual(static_cast<size_t>(6), log.phases.size());
    Assert::IsTrue(log.phases[0].phase == Frontier::Phase::Lock);
    Assert::IsTrue(log.phases[1].phase == Frontier::Phase::Production);
    Assert::IsTrue(log.phases[2].phase == Frontier::Phase::Movement);
    Assert::IsTrue(log.phases[3].phase == Frontier::Phase::Combat);
    Assert::IsTrue(log.phases[4].phase == Frontier::Phase::Claims);
    Assert::IsTrue(log.phases[5].phase == Frontier::Phase::Digest);
    Assert::AreEqual(static_cast<size_t>(6), log.digests.size(), L"one digest per player");
    Assert::IsTrue(after.IsConsistent());
  }

  // ADR-018, at the level that matters most: the resolver is a pure function.
  TEST_METHOD(ResolvingTheSameTickTwiceGivesTheSameState)
  {
    const Frontier::Match start = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(start, 0), .destination = NeighborOf(start, CapitalOf(start, 0))});
    orders.builds.push_back(Frontier::BuildOrder{.system = CapitalOf(start, 0), .kind = Frontier::BuildKind::MiningStation});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    Assert::AreEqual(Advance(start, sets).Hash(), Advance(start, sets).Hash());
    Assert::AreEqual(start.Hash(), SixPlayerMatch().Hash(), L"and resolving did not touch the state it read");
  }

  TEST_METHOD(TenTicksOfNothingAreReproducible)
  {
    const auto run = []
    {
      Frontier::Match match = SixPlayerMatch();
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
    const Frontier::Match start = SixPlayerMatch();
    const Frontier::Match after = AdvanceQuietly(start);

    // One capital, no other systems, no lanes with both ends held: capital rate only.
    const std::uint32_t expected = start.Rules().startingCredits + start.Rules().creditsPerSystem + start.Rules().capitalCreditsBonus;
    Assert::AreEqual(expected, after.PlayerAt(Frontier::PlayerId{0}).credits);
  }

  TEST_METHOD(ALaneWithBothEndsHeldPaysItsOwner)
  {
    Frontier::Match start = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(start, 0);
    const Frontier::SystemId neighbor = NeighborOf(start, capital);
    start.MutableSystems()[neighbor.AsSize()].owner = Frontier::PlayerId{0};

    const Frontier::Match after = AdvanceQuietly(start);

    const std::uint32_t expected = start.Rules().startingCredits + start.Rules().creditsPerSystem + start.Rules().capitalCreditsBonus +
                                   start.Rules().creditsPerSystem + start.Rules().internalLaneIncome;
    Assert::AreEqual(expected, after.PlayerAt(Frontier::PlayerId{0}).credits);
  }

  // The one-pager makes the trade lane pay more than any internal lane, and that gap is the whole
  // reason to talk to a neighbor.
  TEST_METHOD(ATradeLanePaysBothSidesAndPaysMore)
  {
    Frontier::Match start = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(start, 0);
    const Frontier::LaneId lane = start.GalaxyGraph().LanesAt(capital).front();
    const Frontier::SystemId neighbor = start.GalaxyGraph().OtherEnd(lane, capital);
    start.MutableSystems()[neighbor.AsSize()].owner = Frontier::PlayerId{1};
    start.MutableTradeLanes().push_back(Frontier::ActiveTradeLane{.lane = lane, .a = Frontier::PlayerId{0}, .b = Frontier::PlayerId{1}});

    const Frontier::Match after = AdvanceQuietly(start);

    const std::uint32_t before0 = start.PlayerAt(Frontier::PlayerId{0}).credits;
    const std::uint32_t before1 = start.PlayerAt(Frontier::PlayerId{1}).credits;
    const std::uint32_t gained0 = after.PlayerAt(Frontier::PlayerId{0}).credits - before0;
    const std::uint32_t gained1 = after.PlayerAt(Frontier::PlayerId{1}).credits - before1;

    Assert::IsTrue(gained0 >= start.Rules().tradeLaneIncome, L"the proposer is paid");
    Assert::IsTrue(gained1 >= start.Rules().tradeLaneIncome, L"and so is the partner");
    Assert::IsTrue(start.Rules().tradeLaneIncome > start.Rules().internalLaneIncome);
  }

  TEST_METHOD(AShipyardReinforcesTheFleetStandingOnIt)
  {
    Frontier::Match start = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(start, 0);
    start.MutableSystems()[capital.AsSize()].hasShipyard = true;

    const std::uint32_t before = start.FleetAt(FleetOf(start, 0)).ships;
    const Frontier::Match after = AdvanceQuietly(start);

    Assert::AreEqual(before + start.Rules().shipsPerShipyard, after.FleetAt(FleetOf(after, 0)).ships);
  }

  TEST_METHOD(AShipyardWithARivalInTheSystemIsIdle)
  {
    Frontier::Match start = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(start, 0);
    start.MutableSystems()[capital.AsSize()].hasShipyard = true;

    // A rival fleet parks on it.
    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{1};
    raider.ships = 5;
    raider.at = capital;
    (void)start.MutableFleets().push_back(raider);

    const std::uint32_t before = start.FleetAt(FleetOf(start, 0)).ships;

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(start, {}, log);

    Assert::AreEqual(before, after.FleetAt(FleetOf(after, 0)).ships, L"nothing was built");
    Assert::IsTrue(AnyLineContains(log, "is idle"));
  }

  // ---- Phase 3 -------------------------------------------------------------------------------

  TEST_METHOD(AFleetOnAOneTickLaneArrivesThisTick)
  {
    const Frontier::Match start = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(start, 0);

    // A satellite is one tick from its capital by rule.
    const Frontier::LaneId lane = start.GalaxyGraph().LanesAt(capital).front();
    Assert::AreEqual(1U, start.GalaxyGraph().LaneAt(lane).costTicks, L"this test needs a one-tick lane");
    const Frontier::SystemId destination = start.GalaxyGraph().OtherEnd(lane, capital);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(start, 0), .destination = destination});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    const Frontier::Match after = Advance(start, sets);
    const Frontier::MatchFleet& fleet = after.FleetAt(FleetOf(after, 0));

    Assert::IsFalse(fleet.InTransit());
    Assert::IsTrue(fleet.at == destination);
    Assert::IsFalse(fleet.orderedTo.IsValid(), L"the order was consumed");
  }

  TEST_METHOD(ALongerLaneTakesItsFullCost)
  {
    Frontier::Match match = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(match, 0);

    // Find a lane out of the capital that costs more than one.
    Frontier::LaneId slow;
    for (const Frontier::LaneId lane : match.GalaxyGraph().LanesAt(capital))
    {
      if (match.GalaxyGraph().LaneAt(lane).costTicks > 1)
      {
        slow = lane;
        break;
      }
    }
    Assert::IsTrue(slow.IsValid(), L"a capital has a lane out of its cluster");

    const std::uint32_t cost = match.GalaxyGraph().LaneAt(slow).costTicks;
    const Frontier::SystemId destination = match.GalaxyGraph().OtherEnd(slow, capital);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(match, 0), .destination = destination});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    match = Advance(match, sets);
    for (std::uint32_t tick = 1; tick < cost; ++tick)
    {
      Assert::IsTrue(match.FleetAt(FleetOf(match, 0)).InTransit(),
                     (std::wstring(L"still under way after tick ") + std::to_wstring(tick)).c_str());
      match = AdvanceQuietly(match);
    }

    const Frontier::MatchFleet& arrived = match.FleetAt(FleetOf(match, 0));
    Assert::IsFalse(arrived.InTransit());
    Assert::IsTrue(arrived.at == destination);
  }

  // THE ONE-PAGER'S CENTRAL CONSEQUENCE, and it is testable with combat still a no-op: movement is
  // phase 3 and combat is phase 4, so a fleet ordered out is gone before the fight, and the
  // hostile arriving the same tick finds an empty system.
  TEST_METHOD(AFleetOrderedOutIsGoneBeforeTheHostileArrives)
  {
    Frontier::Match start = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(start, 0);
    const Frontier::LaneId lane = start.GalaxyGraph().LanesAt(capital).front();
    const Frontier::SystemId away = start.GalaxyGraph().OtherEnd(lane, capital);

    // A hostile fleet one tick from arriving at the capital.
    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{1};
    raider.ships = 50;
    raider.movingFrom = away;
    raider.movingTo = capital;
    raider.ticksRemaining = 1;
    const Frontier::FleetId raiderId = start.AddFleet(raider);

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(start, 0), .destination = away});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    const Frontier::Match after = Advance(start, sets);

    Assert::IsTrue(after.FleetAt(FleetOf(after, 0)).at == away, L"the defender left");
    Assert::IsTrue(after.FleetAt(raiderId).at == capital, L"the raider arrived");
    Assert::IsFalse(after.FleetAt(FleetOf(after, 0)).destroyed, L"and they never met");
  }

  // ---- Phase 5 -------------------------------------------------------------------------------

  TEST_METHOD(AnUncontestedFleetClaimsAnUnclaimedSystem)
  {
    Frontier::Match start = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(start, 0);
    const Frontier::SystemId target = NeighborOf(start, capital);
    Assert::IsFalse(start.SystemAt(target).owner.IsValid(), L"this test needs an unowned system");

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = FleetOf(start, 0), .destination = target});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(start, sets, log);

    Assert::IsTrue(after.SystemAt(target).owner == Frontier::PlayerId{0});
    Assert::IsTrue(HasDigestKind(log, 0, Frontier::DigestKind::SystemClaimed));
  }

  // "Two surviving hostiles: occupied, unclaimed."
  TEST_METHOD(TwoRivalsInOneUnclaimedSystemClaimNothing)
  {
    Frontier::Match start = SixPlayerMatch();
    const Frontier::SystemId target = NeighborOf(start, CapitalOf(start, 0));

    for (std::int32_t player = 0; player < 2; ++player)
    {
      Frontier::MatchFleet fleet;
      fleet.owner = Frontier::PlayerId{player};
      fleet.ships = 5;
      fleet.at = target;
      (void)start.AddFleet(fleet);
    }

    const Frontier::Match after = AdvanceQuietly(start);
    Assert::IsFalse(after.SystemAt(target).owner.IsValid(), L"occupied, and still nobody's");
  }

  // Siege, then capture: exactly two consecutive uncontested ticks.
  TEST_METHOD(CaptureTakesTwoConsecutiveUncontestedTicks)
  {
    Frontier::Match match = SixPlayerMatch();
    match.SetTick(match.Rules().capitalGuardTicks); // past the guard, so a capital can fall

    const Frontier::SystemId target = CapitalOf(match, 1);
    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{0};
    raider.ships = 50;
    raider.at = target;
    (void)match.AddFleet(raider);

    // The defender's own fleet starts on it, so first move it out of the way.
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = CapitalOf(match, 2);

    Frontier::TickLog first;
    match = Frontier::TickResolver::Resolve(match, {}, first);
    Assert::IsTrue(match.SystemAt(target).owner == Frontier::PlayerId{1}, L"one tick is a siege, not a capture");
    Assert::AreEqual(1U, match.SystemAt(target).siegeTicks);
    Assert::IsTrue(HasDigestKind(first, 1, Frontier::DigestKind::SiegeBegun));

    Frontier::TickLog second;
    match = Frontier::TickResolver::Resolve(match, {}, second);
    Assert::IsTrue(match.SystemAt(target).owner == Frontier::PlayerId{0}, L"the second consecutive tick takes it");
    Assert::AreEqual(0U, match.SystemAt(target).siegeTicks, L"and the siege is spent");
    Assert::IsTrue(HasDigestKind(second, 1, Frontier::DigestKind::SystemLost));
    Assert::IsTrue(HasDigestKind(second, 0, Frontier::DigestKind::SystemClaimed));
  }

  TEST_METHOD(OneContestedTickResetsTheSiege)
  {
    Frontier::Match match = SixPlayerMatch();
    match.SetTick(match.Rules().capitalGuardTicks);

    const Frontier::SystemId target = CapitalOf(match, 1);
    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{0};
    raider.ships = 50;
    raider.at = target;
    const Frontier::FleetId raiderId = match.AddFleet(raider);
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = CapitalOf(match, 2);

    match = AdvanceQuietly(match);
    Assert::AreEqual(1U, match.SystemAt(target).siegeTicks);

    // The owner comes home. The siege breaks.
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = target;
    match = AdvanceQuietly(match);

    Assert::AreEqual(0U, match.SystemAt(target).siegeTicks, L"one contested tick and it starts again from nothing");
    Assert::IsTrue(match.SystemAt(target).owner == Frontier::PlayerId{1});

    // And with the owner gone again it is back to one, not two.
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = CapitalOf(match, 2);
    match = AdvanceQuietly(match);
    Assert::AreEqual(1U, match.SystemAt(target).siegeTicks);
    Assert::IsTrue(match.SystemAt(target).owner == Frontier::PlayerId{1});
    Assert::IsTrue(match.FleetAt(raiderId).at == target);
  }

  // The capital guard is an explicit rule layered on top of the siege rule, not derived from it.
  TEST_METHOD(AGuardedCapitalCannotBeBesieged)
  {
    Frontier::Match match = SixPlayerMatch();
    const Frontier::SystemId target = CapitalOf(match, 1);

    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{0};
    raider.ships = 50;
    raider.at = target;
    (void)match.AddFleet(raider);
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = CapitalOf(match, 2);

    for (std::uint32_t tick = 0; tick < match.Rules().capitalGuardTicks; ++tick)
    {
      match = AdvanceQuietly(match);
      Assert::IsTrue(match.SystemAt(target).owner == Frontier::PlayerId{1},
                     (std::wstring(L"still held at tick ") + std::to_wstring(match.Tick())).c_str());
      Assert::AreEqual(0U, match.SystemAt(target).siegeTicks, L"the guard stops the clock, it does not pause it");
    }

    // The tick the guard lapses, the siege can finally begin.
    match = AdvanceQuietly(match);
    Assert::AreEqual(1U, match.SystemAt(target).siegeTicks);
  }

  TEST_METHOD(TheSealedRegionCannotBeClaimed)
  {
    Frontier::Match match = SixPlayerMatch();
    const Frontier::SystemId region = match.GalaxyGraph().RegionAnchor();

    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{0};
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
    const Frontier::Match start = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.proposals.push_back(Frontier::ProposalOrder{.to = Frontier::PlayerId{2}, .kind = Frontier::ProposalKind::ShareScouting});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(start, sets, log);

    Assert::AreEqual(static_cast<size_t>(1), after.Proposals().size());
    Assert::IsTrue(HasDigestKind(log, 2, Frontier::DigestKind::ProposalReceived));
    Assert::IsFalse(HasDigestKind(log, 3, Frontier::DigestKind::ProposalReceived), L"and in nobody else's");
  }

  TEST_METHOD(AcceptingALaneOpensItAndChargesTheProposer)
  {
    Frontier::Match match = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(match, 0);
    const Frontier::LaneId lane = match.GalaxyGraph().LanesAt(capital).front();
    const Frontier::SystemId neighbor = match.GalaxyGraph().OtherEnd(lane, capital);
    match.MutableSystems()[neighbor.AsSize()].owner = Frontier::PlayerId{1};

    Frontier::OrderSet propose;
    propose.player = Frontier::PlayerId{0};
    propose.proposals.push_back(
      Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::OpenLane, .lane = lane});
    const std::array<Frontier::OrderSet, 1> proposing = {propose};
    match = Advance(match, proposing);

    Assert::AreEqual(static_cast<size_t>(1), match.Proposals().size());
    const Frontier::ProposalId offer = match.Proposals().front().id;
    const std::uint32_t purseBefore = match.PlayerAt(Frontier::PlayerId{0}).credits;

    Frontier::OrderSet accept;
    accept.player = Frontier::PlayerId{1};
    accept.answers.push_back(Frontier::AnswerOrder{.proposal = offer, .answer = Frontier::Answer::Accept});
    const std::array<Frontier::OrderSet, 1> accepting = {accept};

    Frontier::TickLog log;
    match = Frontier::TickResolver::Resolve(match, accepting, log);

    Assert::AreEqual(static_cast<size_t>(1), match.TradeLanes().size(), L"the lane is open");
    Assert::IsTrue(match.Proposals().empty(), L"and the offer is off the table");
    Assert::IsTrue(HasDigestKind(log, 0, Frontier::DigestKind::LaneOpened));
    Assert::IsTrue(HasDigestKind(log, 1, Frontier::DigestKind::LaneOpened));

    // Charged the proposer, then paid both. The net has to be the cost less this tick's income.
    Assert::IsTrue(match.PlayerAt(Frontier::PlayerId{0}).credits <=
                     purseBefore + match.Rules().tradeLaneIncome + match.Rules().creditsPerSystem + match.Rules().capitalCreditsBonus,
                   L"the lane was paid for");
  }

  TEST_METHOD(AnUnansweredProposalIsReportedAsIgnored)
  {
    Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet propose;
    propose.player = Frontier::PlayerId{0};
    propose.proposals.push_back(Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::ShareScouting});
    const std::array<Frontier::OrderSet, 1> proposing = {propose};
    match = Advance(match, proposing);

    bool reported = false;
    for (std::uint32_t tick = 0; tick <= match.Rules().proposalWindowTicks; ++tick)
    {
      Frontier::TickLog log;
      match = Frontier::TickResolver::Resolve(match, {}, log);
      if (HasDigestKind(log, 0, Frontier::DigestKind::ProposalIgnored))
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
    Frontier::Match match = SixPlayerMatch();

    Frontier::OrderSet propose;
    propose.player = Frontier::PlayerId{0};
    propose.proposals.push_back(Frontier::ProposalOrder{.to = Frontier::PlayerId{1}, .kind = Frontier::ProposalKind::ShareScouting});
    const std::array<Frontier::OrderSet, 1> proposing = {propose};
    match = Advance(match, proposing);

    Frontier::OrderSet withdraw;
    withdraw.player = Frontier::PlayerId{0};
    withdraw.withdrawals.push_back(Frontier::WithdrawOrder{.proposal = match.Proposals().front().id});
    const std::array<Frontier::OrderSet, 1> withdrawing = {withdraw};

    Frontier::TickLog log;
    match = Frontier::TickResolver::Resolve(match, withdrawing, log);

    Assert::IsTrue(match.Proposals().empty());
    Assert::IsTrue(HasDigestKind(log, 1, Frontier::DigestKind::ProposalWithdrawn));
  }

  // "Canceled by partner" and "canceled: system lost" are different sentences, and the one-pager
  // is explicit that the digest must distinguish them.
  TEST_METHOD(ATradeLaneCancelsWhenAnEndpointChangesHands)
  {
    Frontier::Match match = SixPlayerMatch();
    match.SetTick(match.Rules().capitalGuardTicks);

    const Frontier::SystemId capital = CapitalOf(match, 0);
    const Frontier::LaneId lane = match.GalaxyGraph().LanesAt(capital).front();
    const Frontier::SystemId neighbor = match.GalaxyGraph().OtherEnd(lane, capital);

    match.MutableSystems()[neighbor.AsSize()].owner = Frontier::PlayerId{1};
    match.MutableTradeLanes().push_back(Frontier::ActiveTradeLane{.lane = lane, .a = Frontier::PlayerId{0}, .b = Frontier::PlayerId{1}});

    // Player 2 takes the neighbor: two ticks of siege on an unguarded, undefended system.
    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{2};
    raider.ships = 50;
    raider.at = neighbor;
    (void)match.AddFleet(raider);

    Frontier::TickLog log;
    for (std::int32_t tick = 0; tick < 2; ++tick)
    {
      match = Frontier::TickResolver::Resolve(match, {}, log);
    }

    Assert::IsTrue(match.SystemAt(neighbor).owner == Frontier::PlayerId{2}, L"the endpoint changed hands");
    Assert::IsTrue(match.TradeLanes().empty(), L"so the lane is gone");
    Assert::IsTrue(HasDigestKind(log, 0, Frontier::DigestKind::LaneCanceled));
    Assert::IsTrue(HasDigestKind(log, 1, Frontier::DigestKind::LaneCanceled));
  }

  TEST_METHOD(ARefusedOrderReachesTheDigestWithItsReason)
  {
    const Frontier::Match start = SixPlayerMatch();

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{0};
    orders.builds.push_back(Frontier::BuildOrder{.system = CapitalOf(start, 4), .kind = Frontier::BuildKind::Shipyard});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(start, sets, log);

    Assert::IsTrue(HasDigestKind(log, 0, Frontier::DigestKind::OrderRejected), L"nothing is dropped silently");
    Assert::IsFalse(after.SystemAt(CapitalOf(after, 4)).hasShipyard, L"and nothing was built");
    Assert::AreEqual(start.PlayerAt(Frontier::PlayerId{0}).credits + after.Rules().creditsPerSystem + after.Rules().capitalCreditsBonus,
                     after.PlayerAt(Frontier::PlayerId{0}).credits, L"nor paid for");
  }

  TEST_METHOD(ASecondOrderSetFromOnePlayerIsDiscarded)
  {
    const Frontier::Match start = SixPlayerMatch();
    const Frontier::SystemId capital = CapitalOf(start, 0);

    Frontier::OrderSet once;
    once.player = Frontier::PlayerId{0};
    once.builds.push_back(Frontier::BuildOrder{.system = capital, .kind = Frontier::BuildKind::MiningStation});

    const std::array<Frontier::OrderSet, 2> twice = {once, once};

    Frontier::TickLog log;
    const Frontier::Match after = Frontier::TickResolver::Resolve(start, twice, log);

    Assert::IsTrue(after.SystemAt(capital).hasMiningStation);
    Assert::AreEqual(start.PlayerAt(Frontier::PlayerId{0}).credits - start.Rules().miningStationCost + after.Rules().creditsPerSystem +
                       after.Rules().capitalCreditsBonus + after.Rules().miningStationCredits,
                     after.PlayerAt(Frontier::PlayerId{0}).credits, L"charged once, not twice");
    Assert::IsTrue(AnyLineContains(log, "submitted twice"));
  }

  // ---- Phase 6 -------------------------------------------------------------------------------

  // ADR-020: sorted by the severity each event carries, most consequential first.
  TEST_METHOD(TheDigestIsSortedByConsequence)
  {
    Frontier::Match match = SixPlayerMatch();
    match.SetTick(match.Rules().capitalGuardTicks);

    // Give player 1 a losing tick and a refused order in the same breath: economy is always there,
    // and losing a system must come out on top of both.
    const Frontier::SystemId target = CapitalOf(match, 1);
    match.MutableSystems()[target.AsSize()].siegeBy = Frontier::PlayerId{0};
    match.MutableSystems()[target.AsSize()].siegeTicks = 1;
    match.MutableFleets()[FleetOf(match, 1).AsSize()].at = CapitalOf(match, 2);

    Frontier::MatchFleet raider;
    raider.owner = Frontier::PlayerId{0};
    raider.ships = 50;
    raider.at = target;
    (void)match.AddFleet(raider);

    Frontier::OrderSet bad;
    bad.player = Frontier::PlayerId{1};
    bad.builds.push_back(Frontier::BuildOrder{.system = CapitalOf(match, 3), .kind = Frontier::BuildKind::Shipyard});
    const std::array<Frontier::OrderSet, 1> sets = {bad};

    Frontier::TickLog log;
    match = Frontier::TickResolver::Resolve(match, sets, log);

    const std::vector<Frontier::DigestEntry>& digest = log.digests[1];
    Assert::IsTrue(digest.size() >= 3, L"a loss, a refusal and the economy line");
    Assert::IsTrue(digest.front().kind == Frontier::DigestKind::SystemLost, L"the top event is the one that changes what you do");

    for (std::size_t index = 1; index < digest.size(); ++index)
    {
      Assert::IsTrue(digest[index - 1].severity >= digest[index].severity, L"sorted descending, with no exceptions");
    }
  }

  TEST_METHOD(EveryPhaseAndDigestKindDescribesItself)
  {
    constexpr std::array<Frontier::Phase, 6> PHASES = {Frontier::Phase::Lock,   Frontier::Phase::Production, Frontier::Phase::Movement,
                                                       Frontier::Phase::Combat, Frontier::Phase::Claims,     Frontier::Phase::Digest};
    std::vector<std::string> seen;
    for (const Frontier::Phase phase : PHASES)
    {
      const char* text = Describe(phase);
      Assert::IsNotNull(text);
      Assert::IsTrue(std::find(seen.begin(), seen.end(), text) == seen.end());
      seen.emplace_back(text);
    }

    // Every DigestKind, by walking the range rather than listing it -- a new kind added without a
    // description should fail here rather than print "unknown" to a player.
    for (std::uint8_t kind = 0; kind <= static_cast<std::uint8_t>(Frontier::DigestKind::Custodian); ++kind)
    {
      const char* text = Describe(static_cast<Frontier::DigestKind>(kind));
      Assert::IsNotNull(text);
      Assert::AreNotEqual("unknown", text);
    }
  }

  // Combat is a no-op in this stage, and the phase runs anyway. A replay written today has to have
  // the same six entries as one written after step 5.
  TEST_METHOD(TheCombatPhaseRunsAndSaysItDidNothing)
  {
    Frontier::TickLog log;
    (void)Frontier::TickResolver::Resolve(SixPlayerMatch(), {}, log);

    const Frontier::PhaseRecord* combat = log.Find(Frontier::Phase::Combat);
    Assert::IsNotNull(combat);
    Assert::AreEqual(static_cast<size_t>(1), combat->lines.size());
  }
};

} // namespace GameLogicTests
