// SnapshotTests.cpp -- what a player is entitled to know.
//
// Step 8 of Design/Plans/4X-01-CoreLoop.md and ADR-022.
//
// THE MOST IMPORTANT TEST IN THIS FILE IS A NEGATIVE ONE. A snapshot is what gets sent, and there
// is no second filter downstream, so a field that leaks here is a field a player can read off the
// wire. `TheSnapshotContainsNothingThePlayerIsNotEntitledTo` is the one that would catch it.

#include "pch.h"
#include "CppUnitTest.h"

#include "Snapshot.h"
#include "TickResolver.h"

#include <algorithm>
#include <format>
#include <array>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint64_t SEED = 0x4652'4F4E'5449'4552ULL;

/// A match that has resolved one tick, so visibility has been computed at least once.
[[nodiscard]] Lockstep::Match Settled(Lockstep::MatchRules _rules = {})
{
  _rules.playerCount = 6;
  Lockstep::Match match = Lockstep::Match::Create(_rules, SEED);
  Lockstep::TickLog log;
  return Lockstep::TickResolver::Resolve(match, {}, log);
}

[[nodiscard]] Lockstep::Match Advance(const Lockstep::Match& _match)
{
  Lockstep::TickLog log;
  return Lockstep::TickResolver::Resolve(_match, {}, log);
}

[[nodiscard]] Lockstep::SystemId NeighborOf(const Lockstep::Match& _match, Lockstep::SystemId _from)
{
  const Lockstep::LaneId lane = _match.GalaxyGraph().LanesAt(_from).front();
  return _match.GalaxyGraph().OtherEnd(lane, _from);
}

/// A system exactly two LANES from `_from`, counting hops rather than ticks.
///
/// Hops, because visibility is measured in lanes and a lane costs two to four ticks on the
/// frontier -- a system three ticks away can be one lane away and perfectly visible, which is what
/// an earlier version of these tests got wrong.
[[nodiscard]] Lockstep::SystemId TwoLanesFrom(const Lockstep::Match& _match, Lockstep::SystemId _from)
{
  const std::size_t count = _match.Systems().size();
  std::vector<std::int32_t> hops(count, -1);
  hops[_from.AsSize()] = 0;

  std::vector<Lockstep::SystemId> ring = {_from};
  for (std::int32_t depth = 0; depth < 2; ++depth)
  {
    std::vector<Lockstep::SystemId> next;
    for (const Lockstep::SystemId at : ring)
    {
      for (const Lockstep::LaneId lane : _match.GalaxyGraph().LanesAt(at))
      {
        const Lockstep::SystemId other = _match.GalaxyGraph().OtherEnd(lane, at);
        if (other.IsValid() && hops[other.AsSize()] < 0)
        {
          hops[other.AsSize()] = depth + 1;
          next.push_back(other);
        }
      }
    }
    ring = next;
  }

  for (std::size_t index = 0; index < count; ++index)
  {
    if (hops[index] == 2)
    {
      return Lockstep::SystemId{static_cast<std::int32_t>(index)};
    }
  }
  return Lockstep::SystemId{};
}

/// A system at least `_ticks` lanes away from every one of `_player`'s systems.
[[nodiscard]] Lockstep::SystemId FarFrom(const Lockstep::Match& _match, std::int32_t _player, std::uint32_t _lanes)
{
  const Lockstep::SystemId capital = _match.GalaxyGraph().Capitals()[static_cast<std::size_t>(_player)];
  const std::vector<std::uint32_t> reach = _match.GalaxyGraph().ShortestPathTicksFrom(capital);

  for (std::size_t index = 0; index < reach.size(); ++index)
  {
    if (reach[index] != Lockstep::Galaxy::UNREACHABLE && reach[index] >= _lanes)
    {
      return Lockstep::SystemId{static_cast<std::int32_t>(index)};
    }
  }
  return Lockstep::SystemId{};
}

} // namespace

/// The wire layout of an `OrderSet`, pinned byte for byte.
///
/// **The match store holds encoded order sets** (ADR-024): a match is its seed and the orders that
/// were locked, and it is reloaded by replaying them. So the order format is not merely a wire
/// format -- it is the file format of every match in progress, and a change to it silently turns
/// every stored match into a different one.
///
/// A round trip cannot catch that. Encoding and decoding with the same wrong code agrees with
/// itself perfectly. What catches it is a literal: these are the bytes this tree produced on
/// 2026-09-12, and any edit that moves a field has to change them here on purpose.
TEST_CLASS(OrderWireFormatTests)
{
public:
  [[nodiscard]] static std::string Hex(const std::vector<std::uint8_t>& _bytes)
  {
    std::string out;
    for (const std::uint8_t byte : _bytes)
    {
      out += std::format("{:02x}", byte);
    }
    return out;
  }

  [[nodiscard]] static Lockstep::OrderSet Everything()
  {
    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{3};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = Lockstep::FleetId{7}, .destination = Lockstep::SystemId{11}});
    orders.builds.push_back(Lockstep::BuildOrder{.system = Lockstep::SystemId{5}, .kind = Lockstep::BuildKind::MiningStation});
    orders.proposals.push_back(Lockstep::ProposalOrder{.to = Lockstep::PlayerId{2},
                                                       .kind = Lockstep::ProposalKind::HoldForTicks,
                                                       .lane = Lockstep::LaneId{9},
                                                       .ticks = 3,
                                                       .conditionalLane = Lockstep::LaneId{4}});
    orders.answers.push_back(Lockstep::AnswerOrder{.proposal = Lockstep::ProposalId{6}, .answer = Lockstep::Answer::Decline});
    orders.withdrawals.push_back(Lockstep::WithdrawOrder{.proposal = Lockstep::ProposalId{8}});
    orders.cancellations.push_back(Lockstep::CancelLaneOrder{.lane = Lockstep::LaneId{1}});
    orders.concede = true;
    return orders;
  }

  TEST_METHOD(AnOrderSetEncodesToExactlyTheseBytes)
  {
    Neuron::ByteWriter writer;
    Everything().Write(writer);

    Assert::AreEqual(std::string{"0300000001000000070000000b0000000100000005000000010100000002000000020900000003000000040000000100000006000"
                                 "000010100000008000000010000000100000001"},
                     Hex(writer.Bytes()), L"the order wire format moved; every match store in progress is now a different match");
  }

  TEST_METHOD(ThoseBytesDecodeBackToWhatMadeThem)
  {
    Neuron::ByteWriter writer;
    Everything().Write(writer);

    Neuron::ByteReader reader{writer.Bytes()};
    const Lockstep::OrderSet back = Lockstep::OrderSet::Read(reader);

    Assert::IsTrue(reader.AtEnd() && !reader.Failed(), L"the pinned bytes did not decode cleanly");
    Assert::AreEqual(3, back.player.Index());
    Assert::AreEqual(std::size_t{1}, back.fleetOrders.size());
    Assert::AreEqual(11, back.fleetOrders[0].destination.Index());
    Assert::IsTrue(back.builds[0].kind == Lockstep::BuildKind::MiningStation);
    Assert::IsTrue(back.proposals[0].kind == Lockstep::ProposalKind::HoldForTicks);
    Assert::AreEqual(3U, back.proposals[0].ticks);
    Assert::IsTrue(back.answers[0].answer == Lockstep::Answer::Decline);
    Assert::AreEqual(std::size_t{1}, back.withdrawals.size());
    Assert::AreEqual(std::size_t{1}, back.cancellations.size());
    Assert::IsTrue(back.concede);
  }
};

TEST_CLASS(VisibilityTests)
{
public:
  // "Own systems, and systems one lane away." The starting cluster's capital and its immediate
  // neighbours, and nothing further.
  TEST_METHOD(YouSeeYourOwnSystemsAndTheirNeighbors)
  {
    const Lockstep::Match match = Settled();
    const Lockstep::SystemId capital = match.GalaxyGraph().Capitals()[0];
    const std::vector<Lockstep::SeenSystem>& seen = match.SeenBy(Lockstep::PlayerId{0});

    Assert::IsTrue(seen[capital.AsSize()].live, L"your own capital");

    for (const Lockstep::LaneId lane : match.GalaxyGraph().LanesAt(capital))
    {
      const Lockstep::SystemId neighbor = match.GalaxyGraph().OtherEnd(lane, capital);
      Assert::IsTrue(seen[neighbor.AsSize()].live, L"and everything one lane from it");
    }
  }

  TEST_METHOD(YouDoNotSeeASystemThreeLanesAway)
  {
    const Lockstep::Match match = Settled();
    const Lockstep::SystemId distant = FarFrom(match, 0, 3);
    Assert::IsTrue(distant.IsValid(), L"this test needs somewhere far away");

    const std::vector<Lockstep::SeenSystem>& seen = match.SeenBy(Lockstep::PlayerId{0});
    Assert::IsFalse(seen[distant.AsSize()].live);
    Assert::IsFalse(seen[distant.AsSize()].known, L"and it has never been seen either");
  }

  // A fleet sees from where it stands. Moving is how the map opens up.
  TEST_METHOD(AFleetLightsUpWhereItGoes)
  {
    Lockstep::Match match = Settled();
    const Lockstep::SystemId capital = match.GalaxyGraph().Capitals()[0];

    // Two lanes out, so it is beyond the one-lane range and dark right now.
    const Lockstep::SystemId beyond = TwoLanesFrom(match, capital);
    Assert::IsTrue(beyond.IsValid());
    Assert::IsFalse(match.SeenBy(Lockstep::PlayerId{0})[beyond.AsSize()].live, L"dark to begin with");

    // A system one lane from it, which is where the fleet has to stand to see it.
    Lockstep::SystemId hop;
    for (const Lockstep::LaneId lane : match.GalaxyGraph().LanesAt(beyond))
    {
      const Lockstep::SystemId candidate = match.GalaxyGraph().OtherEnd(lane, beyond);
      if (match.SeenBy(Lockstep::PlayerId{0})[candidate.AsSize()].live)
      {
        hop = candidate;
        break;
      }
    }
    Assert::IsTrue(hop.IsValid());

    match.MutableFleets()[0].at = hop;
    match = Advance(match);

    Assert::IsTrue(match.SeenBy(Lockstep::PlayerId{0})[beyond.AsSize()].live, L"the fleet moved and the fog lifted");
  }

  // ADR-022: a system once seen stays KNOWN at its last-seen state, with a tick stamp, rather than
  // going dark. Fog that erases what you learned makes a player re-scout ground they paid for.
  TEST_METHOD(ASystemOnceSeenStaysKnownAtItsLastSeenState)
  {
    Lockstep::Match match = Settled();
    const Lockstep::SystemId capital = match.GalaxyGraph().Capitals()[0];

    const Lockstep::SystemId beyond = TwoLanesFrom(match, capital);
    Assert::IsTrue(beyond.IsValid());

    Lockstep::SystemId hop;
    for (const Lockstep::LaneId lane : match.GalaxyGraph().LanesAt(beyond))
    {
      const Lockstep::SystemId candidate = match.GalaxyGraph().OtherEnd(lane, beyond);
      if (match.SeenBy(Lockstep::PlayerId{0})[candidate.AsSize()].live)
      {
        hop = candidate;
        break;
      }
    }
    Assert::IsTrue(hop.IsValid());

    // The forward system belongs to somebody else, so the scout cannot CLAIM it on the way past.
    // Claiming it would keep the view permanently -- correctly, since you can see one lane from
    // anything you hold -- and there would be nothing left for this test to observe.
    match.MutableSystems()[hop.AsSize()].owner = Lockstep::PlayerId{3};

    // Go and look.
    match.MutableFleets()[0].at = hop;
    match = Advance(match);
    Assert::IsTrue(match.SeenBy(Lockstep::PlayerId{0})[beyond.AsSize()].live);
    const std::uint32_t sawAt = match.SeenBy(Lockstep::PlayerId{0})[beyond.AsSize()].asOfTick;

    // Come home. It goes dark but stays known, stamped with when it was seen.
    match.MutableFleets()[0].at = capital;
    match = Advance(match);
    match = Advance(match);

    const Lockstep::SeenSystem& remembered = match.SeenBy(Lockstep::PlayerId{0})[beyond.AsSize()];
    Assert::IsFalse(remembered.live, L"not visible now");
    Assert::IsTrue(remembered.known, L"but not forgotten");
    Assert::AreEqual(sawAt, remembered.asOfTick, L"and honestly labelled with when");
  }

  // "Shared scouting pays visibly, as fog lifting on the map."
  TEST_METHOD(SharedScoutingAddsAPartnersViewAndRemovingItTakesItAway)
  {
    Lockstep::Match match = Settled();
    const Lockstep::SystemId theirCapital = match.GalaxyGraph().Capitals()[3];

    Assert::IsFalse(match.SeenBy(Lockstep::PlayerId{0})[theirCapital.AsSize()].live, L"not without an agreement");

    match.MutableAgreements().push_back(
      Lockstep::Agreement{.kind = Lockstep::AgreementKind::ShareScouting, .a = Lockstep::PlayerId{0}, .b = Lockstep::PlayerId{3}});
    match = Advance(match);

    Assert::IsTrue(match.SeenBy(Lockstep::PlayerId{0})[theirCapital.AsSize()].live, L"their map is your map");
    Assert::IsTrue(match.SeenBy(Lockstep::PlayerId{3})[match.GalaxyGraph().Capitals()[0].AsSize()].live, L"and it runs both ways");

    // Drop the agreement. The live view goes, the memory stays.
    match.MutableAgreements().clear();
    match = Advance(match);

    Assert::IsFalse(match.SeenBy(Lockstep::PlayerId{0})[theirCapital.AsSize()].live, L"the fog comes back");
    Assert::IsTrue(match.SeenBy(Lockstep::PlayerId{0})[theirCapital.AsSize()].known, L"but what was learned is not unlearned");
  }
};

TEST_CLASS(SnapshotTests)
{
public:
  TEST_METHOD(ASnapshotHoldsWhatTheScreenNeeds)
  {
    const Lockstep::Match match = Settled();
    const Lockstep::Snapshot view = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0});

    Assert::IsTrue(view.Viewer() == Lockstep::PlayerId{0});
    Assert::AreEqual(match.Tick(), view.Tick());
    Assert::IsFalse(view.Systems().empty());
    Assert::IsFalse(view.Lanes().empty());
    Assert::AreEqual(static_cast<size_t>(6), view.Standings().size(), L"the score is public");
    Assert::IsTrue(view.RegionAnchor().IsValid());
    Assert::AreEqual(match.Rules().regionOpensAtTick, view.RegionOpensAt());
    Assert::IsFalse(view.IsFinished());
  }

  // "41 systems" over a map showing eleven is not an inconsistency, it is fog. The totals are the
  // authoritative count and the list is what this player has found.
  // A match at tick zero is a real state a client can be shown: the server may hand out snapshots
  // before the first lock, and a screen of zeros would be the player's first impression.
  TEST_METHOD(ASnapshotAtTickZeroIsUsable)
  {
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    const Lockstep::Match fresh = Lockstep::Match::Create(rules, SEED);
    Assert::AreEqual(0U, fresh.Tick());

    const Lockstep::Snapshot view = Lockstep::Snapshot::For(fresh, Lockstep::PlayerId{0});

    Assert::IsTrue(view.Viewer() == Lockstep::PlayerId{0});
    Assert::AreEqual(static_cast<size_t>(6), view.Standings().size());

    // Placement is an ordinal and starts at one. Zero would print as "0TH".
    for (const Lockstep::SnapshotStanding& standing : view.Standings())
    {
      Assert::IsTrue(standing.placement >= 1, L"placement is an ordinal, not an index");
      Assert::IsTrue(standing.placement <= 6);
    }

    // THE ONE THAT MATTERS: a player can see their own capital before a tick has resolved.
    Assert::IsFalse(view.Systems().empty(), L"a fresh match must not be a blank map");
    Assert::IsTrue(view.Knows(fresh.GalaxyGraph().Capitals()[0]), L"starting with your own capital hidden is not fog, it is a bug");
  }

  TEST_METHOD(TheTotalsAreAuthoritativeAndTheListIsNot)
  {
    const Lockstep::Match match = Settled();
    const Lockstep::Snapshot view = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0});

    Assert::AreEqual(match.GalaxyGraph().SystemCount(), view.TotalSystems());
    Assert::IsTrue(view.Systems().size() < view.TotalSystems(), L"and the player has seen only part of it");
    Assert::IsTrue(view.UnclaimedSystems() > 0);
  }

  // THE NEGATIVE TEST. Everything else here checks that something is present; this one checks that
  // something is absent, which is the failure that cannot be fixed downstream.
  TEST_METHOD(TheSnapshotContainsNothingThePlayerIsNotEntitledTo)
  {
    Lockstep::Match match = Settled();

    const Lockstep::SystemId hidden = FarFrom(match, 0, 3);
    Assert::IsTrue(hidden.IsValid());

    // Put something worth knowing on it, and a fleet on it, and an offer between two other players.
    match.MutableSystems()[hidden.AsSize()].owner = Lockstep::PlayerId{3};
    match.MutableSystems()[hidden.AsSize()].hasShipyard = true;

    Lockstep::MatchFleet garrison;
    garrison.owner = Lockstep::PlayerId{3};
    garrison.ships = 99;
    garrison.at = hidden;
    const Lockstep::FleetId secret = match.AddFleet(garrison);

    Lockstep::OpenProposal theirs;
    theirs.id = match.TakeNextProposalId();
    theirs.from = Lockstep::PlayerId{2};
    theirs.to = Lockstep::PlayerId{3};
    theirs.kind = Lockstep::ProposalKind::ShareScouting;
    match.MutableProposals().push_back(theirs);

    match = Advance(match);
    const Lockstep::Snapshot view = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0});

    Assert::IsFalse(view.Knows(hidden), L"a system three lanes away is not in the snapshot at all");

    const auto& fleets = view.Fleets();
    Assert::IsTrue(
      std::none_of(fleets.begin(), fleets.end(), [secret](const Lockstep::SnapshotFleet& _fleet) { return _fleet.id == secret; }),
      L"nor a garrison parked out of sight");

    const auto& proposals = view.Proposals();
    Assert::IsTrue(std::none_of(proposals.begin(), proposals.end(),
                                [](const Lockstep::SnapshotProposal& _proposal) { return _proposal.from == Lockstep::PlayerId{2}; }),
                   L"nor an offer between two other empires");

    const auto& lanes = view.Lanes();
    Assert::IsTrue(std::none_of(lanes.begin(), lanes.end(),
                                [hidden](const Lockstep::SnapshotLane& _lane) { return _lane.a == hidden || _lane.b == hidden; }),
                   L"and no lane runs into the dark, which would say something is there");
  }

  // "Fleets in transit are public once departed." Commitment is blind at the moment of choice and
  // visible afterwards -- that asymmetry is what makes reading a rival's allocation a skill.
  TEST_METHOD(AFleetBecomesVisibleToEverybodyTheTickItDeparts)
  {
    Lockstep::Match match = Settled();
    const Lockstep::SystemId capital = match.GalaxyGraph().Capitals()[3];
    const Lockstep::SystemId hop = NeighborOf(match, capital);

    // Before it moves, player 0 cannot see player 3's fleet at all.
    {
      const Lockstep::Snapshot view = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0});
      const auto& fleets = view.Fleets();
      Assert::IsTrue(std::none_of(fleets.begin(), fleets.end(),
                                  [](const Lockstep::SnapshotFleet& _fleet) { return _fleet.owner == Lockstep::PlayerId{3}; }),
                     L"a garrison at home is not public");
    }

    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{3};
    orders.fleetOrders.push_back(Lockstep::FleetOrder{.fleet = Lockstep::FleetId{3}, .destination = hop});
    const std::array<Lockstep::OrderSet, 1> sets = {orders};

    // A lane long enough that it is still under way when the tick ends.
    Lockstep::LaneId slow;
    for (const Lockstep::LaneId lane : match.GalaxyGraph().LanesAt(capital))
    {
      if (match.GalaxyGraph().LaneAt(lane).costTicks > 1)
      {
        slow = lane;
        break;
      }
    }
    Assert::IsTrue(slow.IsValid());

    Lockstep::OrderSet longHaul;
    longHaul.player = Lockstep::PlayerId{3};
    longHaul.fleetOrders.push_back(
      Lockstep::FleetOrder{.fleet = Lockstep::FleetId{3}, .destination = match.GalaxyGraph().OtherEnd(slow, capital)});
    const std::array<Lockstep::OrderSet, 1> departing = {longHaul};

    Lockstep::TickLog log;
    match = Lockstep::TickResolver::Resolve(match, {.orders = departing}, log);

    const Lockstep::Snapshot view = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0});
    const auto& fleets = view.Fleets();
    Assert::IsTrue(std::any_of(fleets.begin(), fleets.end(), [](const Lockstep::SnapshotFleet& _fleet)
                               { return _fleet.owner == Lockstep::PlayerId{3} && _fleet.ticksRemaining > 0; }),
                   L"once it has departed, everybody can see it");
  }

  // The preview the orders rail draws, computed on the server and exact because the resolver has
  // no inputs the client cannot see (ADR-021).
  TEST_METHOD(AFleetInTransitCarriesAPreviewOfWhatItIsFlyingInto)
  {
    Lockstep::Match match = Settled();
    const Lockstep::SystemId target = match.GalaxyGraph().Capitals()[1];
    const Lockstep::SystemId approach = NeighborOf(match, target);

    Lockstep::MatchFleet attacker;
    attacker.owner = Lockstep::PlayerId{0};
    attacker.ships = 14;
    attacker.movingFrom = approach;
    attacker.movingTo = target;
    attacker.ticksRemaining = 2;
    (void)match.AddFleet(attacker);

    match.MutableFleets()[1].at = target;
    match.MutableFleets()[1].ships = 11;

    const Lockstep::Snapshot view = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0});

    std::string preview;
    for (const Lockstep::SnapshotFleet& fleet : view.Fleets())
    {
      if (fleet.owner == Lockstep::PlayerId{0} && !fleet.preview.empty())
      {
        preview = fleet.preview;
      }
    }

    Assert::AreEqual(std::string("14 v 11 (+def) - 6 left"), preview, L"the same numbers the design reference draws");
  }

  TEST_METHOD(APreviewOfAnEmptySystemIsEmpty)
  {
    Lockstep::Match match = Settled();
    const Lockstep::SystemId capital = match.GalaxyGraph().Capitals()[0];
    const Lockstep::SystemId empty = NeighborOf(match, capital);

    match.MutableFleets()[0].movingFrom = capital;
    match.MutableFleets()[0].movingTo = empty;
    match.MutableFleets()[0].at = Lockstep::SystemId{};
    match.MutableFleets()[0].ticksRemaining = 2;

    const Lockstep::Snapshot view = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0});
    for (const Lockstep::SnapshotFleet& fleet : view.Fleets())
    {
      if (fleet.owner == Lockstep::PlayerId{0})
      {
        Assert::IsTrue(fleet.preview.empty(), L"there is nothing there to fight");
      }
    }
  }

  TEST_METHOD(OnlyYourOwnProposalsAreInYourSnapshot)
  {
    Lockstep::Match match = Settled();

    Lockstep::OpenProposal mine;
    mine.id = match.TakeNextProposalId();
    mine.from = Lockstep::PlayerId{1};
    mine.to = Lockstep::PlayerId{0};
    mine.kind = Lockstep::ProposalKind::ShareScouting;
    mine.openedAt = match.Tick();
    match.MutableProposals().push_back(mine);

    const Lockstep::Snapshot view = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0});
    Assert::AreEqual(static_cast<size_t>(1), view.Proposals().size());
    Assert::IsTrue(view.Proposals().front().to == Lockstep::PlayerId{0});
    Assert::AreEqual(match.Rules().proposalWindowTicks, view.Proposals().front().ticksLeft, L"with its countdown");
  }

  TEST_METHOD(TheCapitalGuardCountdownIsReported)
  {
    Lockstep::Match match = Settled();
    const std::uint32_t left = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0}).CapitalGuardTicksLeft();
    Assert::AreEqual(match.Rules().capitalGuardTicks - match.Tick(), left, L"the visible countdown the one-pager asks for");

    match.SetTick(match.Rules().capitalGuardTicks + 5);
    Assert::AreEqual(0U, Lockstep::Snapshot::For(match, Lockstep::PlayerId{0}).CapitalGuardTicksLeft());
  }

  TEST_METHOD(ASnapshotSurvivesTheRoundTrip)
  {
    Lockstep::Match match = Settled();
    match = Advance(match);
    const Lockstep::Snapshot original = Lockstep::Snapshot::For(match, Lockstep::PlayerId{0});

    Neuron::ByteWriter writer;
    original.Write(writer);

    Neuron::ByteReader reader{writer.Bytes()};
    const Lockstep::Snapshot returned = Lockstep::Snapshot::Read(reader);

    Assert::IsFalse(reader.Failed());
    Assert::IsTrue(reader.AtEnd(), L"and consumes exactly what was written");

    Assert::IsTrue(original.Viewer() == returned.Viewer());
    Assert::AreEqual(original.Tick(), returned.Tick());
    Assert::AreEqual(original.Systems().size(), returned.Systems().size());
    Assert::AreEqual(original.Lanes().size(), returned.Lanes().size());
    Assert::AreEqual(original.Fleets().size(), returned.Fleets().size());
    Assert::AreEqual(original.Standings().size(), returned.Standings().size());
    Assert::AreEqual(original.TotalSystems(), returned.TotalSystems());
    Assert::AreEqual(original.UnclaimedSystems(), returned.UnclaimedSystems());

    for (std::size_t index = 0; index < original.Systems().size(); ++index)
    {
      Assert::IsTrue(original.Systems()[index].id == returned.Systems()[index].id);
      Assert::AreEqual(original.Systems()[index].name, returned.Systems()[index].name);
      Assert::AreEqual(original.Systems()[index].asOfTick, returned.Systems()[index].asOfTick);
      Assert::AreEqual(original.Systems()[index].live, returned.Systems()[index].live);
    }
  }

  TEST_METHOD(ATruncatedSnapshotFailsRatherThanLies)
  {
    const Lockstep::Snapshot original = Lockstep::Snapshot::For(Settled(), Lockstep::PlayerId{0});

    Neuron::ByteWriter writer;
    original.Write(writer);

    for (const std::size_t cut : {std::size_t{0}, std::size_t{5}, writer.Size() / 2, writer.Size() - 1})
    {
      Neuron::ByteReader reader{std::span<const std::uint8_t>{writer.Bytes().data(), cut}};
      (void)Lockstep::Snapshot::Read(reader);
      Assert::IsTrue(reader.Failed(), (std::wstring(L"cut at ") + std::to_wstring(cut)).c_str());
    }
  }

  // A byte that names no digest kind fails the record rather than becoming a kind no switch has a
  // case for. The client reads these off a socket; a server that is not this program can write
  // anything into that byte.
  TEST_METHOD(ADigestKindByteNamingNothingFailsTheRecord)
  {
    Lockstep::DigestEntry entry;
    entry.kind = Lockstep::DigestKind::Economy;
    entry.title = "Production +4";

    Neuron::ByteWriter writer;
    Lockstep::Snapshot::WriteDigest(writer, {entry});

    // Four bytes of count, then the kind.
    std::vector<std::uint8_t> bytes = writer.Bytes();
    bytes[4] = 250;

    Neuron::ByteReader reader{bytes};
    Assert::IsTrue(Lockstep::Snapshot::ReadDigest(reader).empty());
    Assert::IsTrue(reader.Failed());
  }

  TEST_METHOD(ADigestIsFetchedPerPlayer)
  {
    Lockstep::Match match = Settled();
    Lockstep::TickLog log;
    (void)Lockstep::TickResolver::Resolve(match, {}, log);

    const std::vector<Lockstep::DigestEntry> mine = Lockstep::Snapshot::DigestFor(log, Lockstep::PlayerId{0});
    Assert::IsFalse(mine.empty(), L"the economy line at least");

    Assert::IsTrue(Lockstep::Snapshot::DigestFor(log, Lockstep::PlayerId{99}).empty(), L"and nobody else's");
    Assert::IsTrue(Lockstep::Snapshot::DigestFor(log, Lockstep::PlayerId{}).empty());
  }
};

} // namespace GameLogicTests
