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
[[nodiscard]] Frontier::Match Settled(Frontier::MatchRules _rules = {})
{
  _rules.playerCount = 6;
  Frontier::Match match = Frontier::Match::Create(_rules, SEED);
  Frontier::TickLog log;
  return Frontier::TickResolver::Resolve(match, {}, log);
}

[[nodiscard]] Frontier::Match Advance(const Frontier::Match& _match)
{
  Frontier::TickLog log;
  return Frontier::TickResolver::Resolve(_match, {}, log);
}

[[nodiscard]] Frontier::SystemId NeighborOf(const Frontier::Match& _match, Frontier::SystemId _from)
{
  const Frontier::LaneId lane = _match.GalaxyGraph().LanesAt(_from).front();
  return _match.GalaxyGraph().OtherEnd(lane, _from);
}

/// A system exactly two LANES from `_from`, counting hops rather than ticks.
///
/// Hops, because visibility is measured in lanes and a lane costs two to four ticks on the
/// frontier -- a system three ticks away can be one lane away and perfectly visible, which is what
/// an earlier version of these tests got wrong.
[[nodiscard]] Frontier::SystemId TwoLanesFrom(const Frontier::Match& _match, Frontier::SystemId _from)
{
  const std::size_t count = _match.Systems().size();
  std::vector<std::int32_t> hops(count, -1);
  hops[_from.AsSize()] = 0;

  std::vector<Frontier::SystemId> ring = {_from};
  for (std::int32_t depth = 0; depth < 2; ++depth)
  {
    std::vector<Frontier::SystemId> next;
    for (const Frontier::SystemId at : ring)
    {
      for (const Frontier::LaneId lane : _match.GalaxyGraph().LanesAt(at))
      {
        const Frontier::SystemId other = _match.GalaxyGraph().OtherEnd(lane, at);
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
      return Frontier::SystemId{static_cast<std::int32_t>(index)};
    }
  }
  return Frontier::SystemId{};
}

/// A system at least `_ticks` lanes away from every one of `_player`'s systems.
[[nodiscard]] Frontier::SystemId FarFrom(const Frontier::Match& _match, std::int32_t _player, std::uint32_t _lanes)
{
  const Frontier::SystemId capital = _match.GalaxyGraph().Capitals()[static_cast<std::size_t>(_player)];
  const std::vector<std::uint32_t> reach = _match.GalaxyGraph().ShortestPathTicksFrom(capital);

  for (std::size_t index = 0; index < reach.size(); ++index)
  {
    if (reach[index] != Frontier::Galaxy::UNREACHABLE && reach[index] >= _lanes)
    {
      return Frontier::SystemId{static_cast<std::int32_t>(index)};
    }
  }
  return Frontier::SystemId{};
}

} // namespace

TEST_CLASS(VisibilityTests)
{
public:
  // "Own systems, and systems one lane away." The starting cluster's capital and its immediate
  // neighbours, and nothing further.
  TEST_METHOD(YouSeeYourOwnSystemsAndTheirNeighbors)
  {
    const Frontier::Match match = Settled();
    const Frontier::SystemId capital = match.GalaxyGraph().Capitals()[0];
    const std::vector<Frontier::SeenSystem>& seen = match.SeenBy(Frontier::PlayerId{0});

    Assert::IsTrue(seen[capital.AsSize()].live, L"your own capital");

    for (const Frontier::LaneId lane : match.GalaxyGraph().LanesAt(capital))
    {
      const Frontier::SystemId neighbor = match.GalaxyGraph().OtherEnd(lane, capital);
      Assert::IsTrue(seen[neighbor.AsSize()].live, L"and everything one lane from it");
    }
  }

  TEST_METHOD(YouDoNotSeeASystemThreeLanesAway)
  {
    const Frontier::Match match = Settled();
    const Frontier::SystemId distant = FarFrom(match, 0, 3);
    Assert::IsTrue(distant.IsValid(), L"this test needs somewhere far away");

    const std::vector<Frontier::SeenSystem>& seen = match.SeenBy(Frontier::PlayerId{0});
    Assert::IsFalse(seen[distant.AsSize()].live);
    Assert::IsFalse(seen[distant.AsSize()].known, L"and it has never been seen either");
  }

  // A fleet sees from where it stands. Moving is how the map opens up.
  TEST_METHOD(AFleetLightsUpWhereItGoes)
  {
    Frontier::Match match = Settled();
    const Frontier::SystemId capital = match.GalaxyGraph().Capitals()[0];

    // Two lanes out, so it is beyond the one-lane range and dark right now.
    const Frontier::SystemId beyond = TwoLanesFrom(match, capital);
    Assert::IsTrue(beyond.IsValid());
    Assert::IsFalse(match.SeenBy(Frontier::PlayerId{0})[beyond.AsSize()].live, L"dark to begin with");

    // A system one lane from it, which is where the fleet has to stand to see it.
    Frontier::SystemId hop;
    for (const Frontier::LaneId lane : match.GalaxyGraph().LanesAt(beyond))
    {
      const Frontier::SystemId candidate = match.GalaxyGraph().OtherEnd(lane, beyond);
      if (match.SeenBy(Frontier::PlayerId{0})[candidate.AsSize()].live)
      {
        hop = candidate;
        break;
      }
    }
    Assert::IsTrue(hop.IsValid());

    match.MutableFleets()[0].at = hop;
    match = Advance(match);

    Assert::IsTrue(match.SeenBy(Frontier::PlayerId{0})[beyond.AsSize()].live, L"the fleet moved and the fog lifted");
  }

  // ADR-022: a system once seen stays KNOWN at its last-seen state, with a tick stamp, rather than
  // going dark. Fog that erases what you learned makes a player re-scout ground they paid for.
  TEST_METHOD(ASystemOnceSeenStaysKnownAtItsLastSeenState)
  {
    Frontier::Match match = Settled();
    const Frontier::SystemId capital = match.GalaxyGraph().Capitals()[0];

    const Frontier::SystemId beyond = TwoLanesFrom(match, capital);
    Assert::IsTrue(beyond.IsValid());

    Frontier::SystemId hop;
    for (const Frontier::LaneId lane : match.GalaxyGraph().LanesAt(beyond))
    {
      const Frontier::SystemId candidate = match.GalaxyGraph().OtherEnd(lane, beyond);
      if (match.SeenBy(Frontier::PlayerId{0})[candidate.AsSize()].live)
      {
        hop = candidate;
        break;
      }
    }
    Assert::IsTrue(hop.IsValid());

    // The forward system belongs to somebody else, so the scout cannot CLAIM it on the way past.
    // Claiming it would keep the view permanently -- correctly, since you can see one lane from
    // anything you hold -- and there would be nothing left for this test to observe.
    match.MutableSystems()[hop.AsSize()].owner = Frontier::PlayerId{3};

    // Go and look.
    match.MutableFleets()[0].at = hop;
    match = Advance(match);
    Assert::IsTrue(match.SeenBy(Frontier::PlayerId{0})[beyond.AsSize()].live);
    const std::uint32_t sawAt = match.SeenBy(Frontier::PlayerId{0})[beyond.AsSize()].asOfTick;

    // Come home. It goes dark but stays known, stamped with when it was seen.
    match.MutableFleets()[0].at = capital;
    match = Advance(match);
    match = Advance(match);

    const Frontier::SeenSystem& remembered = match.SeenBy(Frontier::PlayerId{0})[beyond.AsSize()];
    Assert::IsFalse(remembered.live, L"not visible now");
    Assert::IsTrue(remembered.known, L"but not forgotten");
    Assert::AreEqual(sawAt, remembered.asOfTick, L"and honestly labelled with when");
  }

  // "Shared scouting pays visibly, as fog lifting on the map."
  TEST_METHOD(SharedScoutingAddsAPartnersViewAndRemovingItTakesItAway)
  {
    Frontier::Match match = Settled();
    const Frontier::SystemId theirCapital = match.GalaxyGraph().Capitals()[3];

    Assert::IsFalse(match.SeenBy(Frontier::PlayerId{0})[theirCapital.AsSize()].live, L"not without an agreement");

    match.MutableAgreements().push_back(
      Frontier::Agreement{.kind = Frontier::AgreementKind::ShareScouting, .a = Frontier::PlayerId{0}, .b = Frontier::PlayerId{3}});
    match = Advance(match);

    Assert::IsTrue(match.SeenBy(Frontier::PlayerId{0})[theirCapital.AsSize()].live, L"their map is your map");
    Assert::IsTrue(match.SeenBy(Frontier::PlayerId{3})[match.GalaxyGraph().Capitals()[0].AsSize()].live, L"and it runs both ways");

    // Drop the agreement. The live view goes, the memory stays.
    match.MutableAgreements().clear();
    match = Advance(match);

    Assert::IsFalse(match.SeenBy(Frontier::PlayerId{0})[theirCapital.AsSize()].live, L"the fog comes back");
    Assert::IsTrue(match.SeenBy(Frontier::PlayerId{0})[theirCapital.AsSize()].known, L"but what was learned is not unlearned");
  }
};

TEST_CLASS(SnapshotTests)
{
public:
  TEST_METHOD(ASnapshotHoldsWhatTheScreenNeeds)
  {
    const Frontier::Match match = Settled();
    const Frontier::Snapshot view = Frontier::Snapshot::For(match, Frontier::PlayerId{0});

    Assert::IsTrue(view.Viewer() == Frontier::PlayerId{0});
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
    Frontier::MatchRules rules;
    rules.playerCount = 6;
    const Frontier::Match fresh = Frontier::Match::Create(rules, SEED);
    Assert::AreEqual(0U, fresh.Tick());

    const Frontier::Snapshot view = Frontier::Snapshot::For(fresh, Frontier::PlayerId{0});

    Assert::IsTrue(view.Viewer() == Frontier::PlayerId{0});
    Assert::AreEqual(static_cast<size_t>(6), view.Standings().size());

    // Placement is an ordinal and starts at one. Zero would print as "0TH".
    for (const Frontier::SnapshotStanding& standing : view.Standings())
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
    const Frontier::Match match = Settled();
    const Frontier::Snapshot view = Frontier::Snapshot::For(match, Frontier::PlayerId{0});

    Assert::AreEqual(match.GalaxyGraph().SystemCount(), view.TotalSystems());
    Assert::IsTrue(view.Systems().size() < view.TotalSystems(), L"and the player has seen only part of it");
    Assert::IsTrue(view.UnclaimedSystems() > 0);
  }

  // THE NEGATIVE TEST. Everything else here checks that something is present; this one checks that
  // something is absent, which is the failure that cannot be fixed downstream.
  TEST_METHOD(TheSnapshotContainsNothingThePlayerIsNotEntitledTo)
  {
    Frontier::Match match = Settled();

    const Frontier::SystemId hidden = FarFrom(match, 0, 3);
    Assert::IsTrue(hidden.IsValid());

    // Put something worth knowing on it, and a fleet on it, and an offer between two other players.
    match.MutableSystems()[hidden.AsSize()].owner = Frontier::PlayerId{3};
    match.MutableSystems()[hidden.AsSize()].hasShipyard = true;

    Frontier::MatchFleet garrison;
    garrison.owner = Frontier::PlayerId{3};
    garrison.ships = 99;
    garrison.at = hidden;
    const Frontier::FleetId secret = match.AddFleet(garrison);

    Frontier::OpenProposal theirs;
    theirs.id = match.TakeNextProposalId();
    theirs.from = Frontier::PlayerId{2};
    theirs.to = Frontier::PlayerId{3};
    theirs.kind = Frontier::ProposalKind::ShareScouting;
    match.MutableProposals().push_back(theirs);

    match = Advance(match);
    const Frontier::Snapshot view = Frontier::Snapshot::For(match, Frontier::PlayerId{0});

    Assert::IsFalse(view.Knows(hidden), L"a system three lanes away is not in the snapshot at all");

    const auto& fleets = view.Fleets();
    Assert::IsTrue(
      std::none_of(fleets.begin(), fleets.end(), [secret](const Frontier::SnapshotFleet& _fleet) { return _fleet.id == secret; }),
      L"nor a garrison parked out of sight");

    const auto& proposals = view.Proposals();
    Assert::IsTrue(std::none_of(proposals.begin(), proposals.end(),
                                [](const Frontier::SnapshotProposal& _proposal) { return _proposal.from == Frontier::PlayerId{2}; }),
                   L"nor an offer between two other empires");

    const auto& lanes = view.Lanes();
    Assert::IsTrue(std::none_of(lanes.begin(), lanes.end(),
                                [hidden](const Frontier::SnapshotLane& _lane) { return _lane.a == hidden || _lane.b == hidden; }),
                   L"and no lane runs into the dark, which would say something is there");
  }

  // "Fleets in transit are public once departed." Commitment is blind at the moment of choice and
  // visible afterwards -- that asymmetry is what makes reading a rival's allocation a skill.
  TEST_METHOD(AFleetBecomesVisibleToEverybodyTheTickItDeparts)
  {
    Frontier::Match match = Settled();
    const Frontier::SystemId capital = match.GalaxyGraph().Capitals()[3];
    const Frontier::SystemId hop = NeighborOf(match, capital);

    // Before it moves, player 0 cannot see player 3's fleet at all.
    {
      const Frontier::Snapshot view = Frontier::Snapshot::For(match, Frontier::PlayerId{0});
      const auto& fleets = view.Fleets();
      Assert::IsTrue(std::none_of(fleets.begin(), fleets.end(),
                                  [](const Frontier::SnapshotFleet& _fleet) { return _fleet.owner == Frontier::PlayerId{3}; }),
                     L"a garrison at home is not public");
    }

    Frontier::OrderSet orders;
    orders.player = Frontier::PlayerId{3};
    orders.fleetOrders.push_back(Frontier::FleetOrder{.fleet = Frontier::FleetId{3}, .destination = hop});
    const std::array<Frontier::OrderSet, 1> sets = {orders};

    // A lane long enough that it is still under way when the tick ends.
    Frontier::LaneId slow;
    for (const Frontier::LaneId lane : match.GalaxyGraph().LanesAt(capital))
    {
      if (match.GalaxyGraph().LaneAt(lane).costTicks > 1)
      {
        slow = lane;
        break;
      }
    }
    Assert::IsTrue(slow.IsValid());

    Frontier::OrderSet longHaul;
    longHaul.player = Frontier::PlayerId{3};
    longHaul.fleetOrders.push_back(
      Frontier::FleetOrder{.fleet = Frontier::FleetId{3}, .destination = match.GalaxyGraph().OtherEnd(slow, capital)});
    const std::array<Frontier::OrderSet, 1> departing = {longHaul};

    Frontier::TickLog log;
    match = Frontier::TickResolver::Resolve(match, {.orders = departing}, log);

    const Frontier::Snapshot view = Frontier::Snapshot::For(match, Frontier::PlayerId{0});
    const auto& fleets = view.Fleets();
    Assert::IsTrue(std::any_of(fleets.begin(), fleets.end(), [](const Frontier::SnapshotFleet& _fleet)
                               { return _fleet.owner == Frontier::PlayerId{3} && _fleet.ticksRemaining > 0; }),
                   L"once it has departed, everybody can see it");
  }

  // The preview the orders rail draws, computed on the server and exact because the resolver has
  // no inputs the client cannot see (ADR-021).
  TEST_METHOD(AFleetInTransitCarriesAPreviewOfWhatItIsFlyingInto)
  {
    Frontier::Match match = Settled();
    const Frontier::SystemId target = match.GalaxyGraph().Capitals()[1];
    const Frontier::SystemId approach = NeighborOf(match, target);

    Frontier::MatchFleet attacker;
    attacker.owner = Frontier::PlayerId{0};
    attacker.ships = 14;
    attacker.movingFrom = approach;
    attacker.movingTo = target;
    attacker.ticksRemaining = 2;
    (void)match.AddFleet(attacker);

    match.MutableFleets()[1].at = target;
    match.MutableFleets()[1].ships = 11;

    const Frontier::Snapshot view = Frontier::Snapshot::For(match, Frontier::PlayerId{0});

    std::string preview;
    for (const Frontier::SnapshotFleet& fleet : view.Fleets())
    {
      if (fleet.owner == Frontier::PlayerId{0} && !fleet.preview.empty())
      {
        preview = fleet.preview;
      }
    }

    Assert::AreEqual(std::string("14 v 11 (+def) - 6 left"), preview, L"the same numbers the design reference draws");
  }

  TEST_METHOD(APreviewOfAnEmptySystemIsEmpty)
  {
    Frontier::Match match = Settled();
    const Frontier::SystemId capital = match.GalaxyGraph().Capitals()[0];
    const Frontier::SystemId empty = NeighborOf(match, capital);

    match.MutableFleets()[0].movingFrom = capital;
    match.MutableFleets()[0].movingTo = empty;
    match.MutableFleets()[0].at = Frontier::SystemId{};
    match.MutableFleets()[0].ticksRemaining = 2;

    const Frontier::Snapshot view = Frontier::Snapshot::For(match, Frontier::PlayerId{0});
    for (const Frontier::SnapshotFleet& fleet : view.Fleets())
    {
      if (fleet.owner == Frontier::PlayerId{0})
      {
        Assert::IsTrue(fleet.preview.empty(), L"there is nothing there to fight");
      }
    }
  }

  TEST_METHOD(OnlyYourOwnProposalsAreInYourSnapshot)
  {
    Frontier::Match match = Settled();

    Frontier::OpenProposal mine;
    mine.id = match.TakeNextProposalId();
    mine.from = Frontier::PlayerId{1};
    mine.to = Frontier::PlayerId{0};
    mine.kind = Frontier::ProposalKind::ShareScouting;
    mine.openedAt = match.Tick();
    match.MutableProposals().push_back(mine);

    const Frontier::Snapshot view = Frontier::Snapshot::For(match, Frontier::PlayerId{0});
    Assert::AreEqual(static_cast<size_t>(1), view.Proposals().size());
    Assert::IsTrue(view.Proposals().front().to == Frontier::PlayerId{0});
    Assert::AreEqual(match.Rules().proposalWindowTicks, view.Proposals().front().ticksLeft, L"with its countdown");
  }

  TEST_METHOD(TheCapitalGuardCountdownIsReported)
  {
    Frontier::Match match = Settled();
    const std::uint32_t left = Frontier::Snapshot::For(match, Frontier::PlayerId{0}).CapitalGuardTicksLeft();
    Assert::AreEqual(match.Rules().capitalGuardTicks - match.Tick(), left, L"the visible countdown the one-pager asks for");

    match.SetTick(match.Rules().capitalGuardTicks + 5);
    Assert::AreEqual(0U, Frontier::Snapshot::For(match, Frontier::PlayerId{0}).CapitalGuardTicksLeft());
  }

  TEST_METHOD(ASnapshotSurvivesTheRoundTrip)
  {
    Frontier::Match match = Settled();
    match = Advance(match);
    const Frontier::Snapshot original = Frontier::Snapshot::For(match, Frontier::PlayerId{0});

    Neuron::ByteWriter writer;
    original.Write(writer);

    Neuron::ByteReader reader{writer.Bytes()};
    const Frontier::Snapshot returned = Frontier::Snapshot::Read(reader);

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
    const Frontier::Snapshot original = Frontier::Snapshot::For(Settled(), Frontier::PlayerId{0});

    Neuron::ByteWriter writer;
    original.Write(writer);

    for (const std::size_t cut : {std::size_t{0}, std::size_t{5}, writer.Size() / 2, writer.Size() - 1})
    {
      Neuron::ByteReader reader{std::span<const std::uint8_t>{writer.Bytes().data(), cut}};
      (void)Frontier::Snapshot::Read(reader);
      Assert::IsTrue(reader.Failed(), (std::wstring(L"cut at ") + std::to_wstring(cut)).c_str());
    }
  }

  TEST_METHOD(ADigestIsFetchedPerPlayer)
  {
    Frontier::Match match = Settled();
    Frontier::TickLog log;
    (void)Frontier::TickResolver::Resolve(match, {}, log);

    const std::vector<Frontier::DigestEntry> mine = Frontier::Snapshot::DigestFor(log, Frontier::PlayerId{0});
    Assert::IsFalse(mine.empty(), L"the economy line at least");

    Assert::IsTrue(Frontier::Snapshot::DigestFor(log, Frontier::PlayerId{99}).empty(), L"and nobody else's");
    Assert::IsTrue(Frontier::Snapshot::DigestFor(log, Frontier::PlayerId{}).empty());
  }
};

} // namespace GameLogicTests
