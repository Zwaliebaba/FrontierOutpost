// MapTapTests.cpp -- the map pane, tapped: a fleet's move (ADR-077), the garrison badge (ADR-079), the
// move mode (ADR-114), the camera (ADR-017), and the stations it draws (ADR-103).

#include "pch.h"
#include "CppUnitTest.h"

#include "Headless.h"

#include "DigestView.h"
#include "MainPage.h"
#include "SnapshotView.h"

#include "BotPolicy.h"
#include "MatchSimulation.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

// A fleet takes an order while it is standing still, and the screen offers one only then
// (ADR-077). The defect these pin was reported on two screenshots of a practice match: a board with
// credits in hand, a fleet parked at a held system, and no control anywhere that would move it.
TEST_CLASS(FleetMoveTapTests)
{
public:
  /// Six bots and no human seat, so the viewer's own fleet is the one thing on the board that does
  /// not move unless this test moves it -- and every other seat's does, which is what puts a fleet
  /// in transit on the map for the tests that need one.
  [[nodiscard]] static std::unique_ptr<Lockstep::MatchSimulation> EverybodyMoves(std::int32_t _ticks)
  {
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    const std::vector<std::optional<Lockstep::BotPolicy>> bots(6, std::optional<Lockstep::BotPolicy>{Lockstep::BotPolicy::ExpandNear});
    auto simulation = std::make_unique<Lockstep::MatchSimulation>(rules, 0x5349'474E'414C'5321ULL, bots);
    for (std::int32_t tick = 0; tick < _ticks; ++tick)
    {
      simulation->Resolve();
    }
    return simulation;
  }

  /// Which of the viewer's fleets is standing at a system, or `EventRefs::NONE`.
  [[nodiscard]] static std::int32_t StandingFleet(const Lockstep::MatchState& _state)
  {
    for (std::size_t index = 0; index < _state.fleets.size(); ++index)
    {
      const Lockstep::Fleet& fleet = _state.fleets[index];
      if (fleet.owner == _state.viewer && !fleet.underWay)
      {
        return static_cast<std::int32_t>(index);
      }
    }
    return Lockstep::EventRefs::NONE;
  }

  TEST_METHOD(AStandingFleetCanBeSentSomewhereFromTheScreen)
  {
    // **The tick this is about is any tick after the first.** The digest's `MOVE` is a standing
    // move, offered only when no card can be acted on (ADR-056), and a production line the player
    // can build from is a card that can -- so from T1 onwards the digest carries a BUILD button and
    // no MOVE at all. The map draws no marker for a parked fleet (ADR-059), which leaves the rail.
    const auto simulation = PlayedMatch(4);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    const std::int32_t standing = StandingFleet(page.State());
    Assert::IsTrue(standing != Lockstep::EventRefs::NONE, L"four ticks in and the viewer has no fleet standing anywhere");

    const bool digestOffersAMove =
      std::any_of(page.State().digest.begin(), page.State().digest.end(),
                  [](const Lockstep::DigestEvent& _event)
                  {
                    return std::any_of(_event.actions.begin(), _event.actions.end(), [](const Lockstep::EventAction& _action)
                                       { return _action.kind == Lockstep::EventActionKind::RedirectFleet; });
                  });
    Assert::IsFalse(digestOffersAMove, L"the digest offered a MOVE, so this fixture is not the board the defect was reported on");

    // Sweep the whole screen, exactly as a finger would hunt for the control. **Three stages since
    // ADR-114**, because the order takes three taps: one to take the move onto the map, one to light
    // a destination, and the filled `SEND` that commits it. The third is what makes a slipped finger
    // on the map cost a tap rather than a tick.
    Headless renderers;
    const bool onTheMap =
      SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT, [&page] { return page.MoveOrder().has_value(); });
    Assert::IsTrue(onTheMap, L"nothing on the screen takes a standing fleet's move onto the map");

    const bool lit = SweepFor(
      page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
      [&page] { return page.MoveOrder().has_value() && page.MoveOrder()->selected != Lockstep::EventRefs::NONE; }, true,
      [&page, &renderers]
      {
        // A stray tap on the map leaves the mode, which is what tapping the board means while a
        // question is open. Put it back, so the sweep is looking for a destination rather than for
        // the mode it already found.
        if (page.MoveOrder().has_value())
        {
          return false;
        }
        return SweepFor(page, renderers, DrawPage, SCREEN_WIDTH - ORDERS_RAIL, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                        [&page] { return page.MoveOrder().has_value(); });
      });
    Assert::IsTrue(lit, L"the mode opened and nothing in it lit a destination");

    const std::int32_t where = page.State().fleets[static_cast<std::size_t>(standing)].from;
    renderers.Begin();
    DrawPage(page, renderers);
    const auto send = std::ranges::find_if(page.Hits(), [](const Lockstep::MainPage::HitRegion& _hit)
                                           { return _hit.action == Lockstep::MainPage::Action::SendMove; });
    Assert::IsTrue(send != page.Hits().end(), L"a destination is lit and there is nothing on the screen that sends it");
    (void)page.HandleTap(send->x + send->width * 0.5F, send->y + send->height * 0.5F);

    Assert::AreNotEqual(where, page.State().fleets[static_cast<std::size_t>(standing)].to, L"SEND ordered the fleet nowhere");
    Assert::IsFalse(page.MoveOrder().has_value(), L"SEND left the mode open");

    const Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
    Assert::AreEqual(std::size_t{1}, orders.fleetOrders.size(), L"the move did not become an order");
  }

  TEST_METHOD(AFleetAlreadyOnALaneTakesNoOrderFromAnyTap)
  {
    // `Match::Validate` refuses a second order on a fleet in transit, so a control that offered one
    // is a control whose order the lock is certain to refuse -- the thing ADR-053 took off this
    // screen, applied to the one surface that still had it.
    bool sawOne = false;
    for (std::int32_t ticks = 1; ticks <= 14 && !sawOne; ++ticks)
    {
      const auto simulation = EverybodyMoves(ticks);
      Lockstep::MatchState state = ViewOfSeatZero(*simulation);

      const auto flying = std::find_if(state.fleets.begin(), state.fleets.end(),
                                       [&state](const Lockstep::Fleet& _fleet) { return _fleet.owner == state.viewer && _fleet.underWay; });
      if (flying == state.fleets.end())
      {
        continue;
      }
      sawOne = true;

      const auto at = static_cast<std::size_t>(std::distance(state.fleets.begin(), flying));
      const std::int32_t id = flying->id;
      const std::int32_t destination = flying->to;

      Lockstep::MainPage page;
      page.Create(std::move(state));

      // The whole screen, and the viewer's OTHER fleets are ordered around freely on the way --
      // what is claimed is about this one. `_done` is the defect: if any tap ever redirects it, or
      // puts an order for it on the wire, the sweep stops there and the assertion below says so.
      Headless renderers;
      const bool touched =
        SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                 [&page, at, id, destination]
                 {
                   if (page.State().fleets[at].to != destination)
                   {
                     return true;
                   }
                   const Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
                   return std::any_of(orders.fleetOrders.begin(), orders.fleetOrders.end(),
                                      [id](const Lockstep::FleetOrder& _order) { return _order.fleet == Lockstep::FleetId{id}; });
                 });
      Assert::IsFalse(touched, L"a tap gave a fleet already on a lane an order the lock is certain to refuse");
    }
    Assert::IsTrue(sawOne, L"fourteen ticks of six bots put nothing of the viewer's in transit, so this test proved nothing");
  }

  TEST_METHOD(AFleetInTransitIsNotOrderedAgainEveryTime)
  {
    // **Every fleet order this client sent for a fleet already flying came back refused.** The
    // snapshot reports an in-transit fleet as `Move`, and one order per moving fleet therefore
    // re-sent it on every tap -- `Order refused -- that fleet is already under way`, once per tick,
    // for an order the player never gave (ADR-077).
    bool sawOne = false;
    for (std::int32_t ticks = 1; ticks <= 14 && !sawOne; ++ticks)
    {
      const auto simulation = EverybodyMoves(ticks);
      const Lockstep::MatchState state = ViewOfSeatZero(*simulation);
      if (std::none_of(state.fleets.begin(), state.fleets.end(),
                       [&state](const Lockstep::Fleet& _fleet) { return _fleet.owner == state.viewer && _fleet.underWay; }))
      {
        continue;
      }
      sawOne = true;

      Lockstep::OrderSet orders = Lockstep::OrdersOf(state);
      orders.player = Lockstep::PlayerId{0};

      const std::vector<Lockstep::RejectedOrder> rejected = simulation->State().Validate(orders);
      const bool refused = std::any_of(rejected.begin(), rejected.end(), [](const Lockstep::RejectedOrder& _refusal)
                                       { return _refusal.reason == Lockstep::OrderRejection::FleetInTransit; });
      Assert::IsFalse(refused, L"the client sent an order for a fleet already under way, and the lock refused it");
    }
    Assert::IsTrue(sawOne, L"fourteen ticks of six bots put nothing of the viewer's in transit, so this test proved nothing");
  }
};

// A fleet standing at a system is drawn there and can be tapped there (ADR-079). The map pane only:
// the locks rail reaches the same picker (ADR-077) and would answer every one of these for the wrong
// reason, so every sweep below stops at the rail's left edge.
TEST_CLASS(GarrisonBadgeTapTests)
{
public:
  static constexpr std::int32_t MAP_LEFT = 400;
  static constexpr std::int32_t MAP_RIGHT = SCREEN_WIDTH - ORDERS_RAIL;

  /// Where the viewer's one fleet is standing, as a system position.
  [[nodiscard]] static std::int32_t WhereTheFleetStands(const Lockstep::MatchState& _state)
  {
    for (const Lockstep::Fleet& fleet : _state.fleets)
    {
      if (fleet.owner == _state.viewer && !fleet.OnALane())
      {
        return fleet.to;
      }
    }
    return Lockstep::EventRefs::NONE;
  }

  TEST_METHOD(ABadgeOpensThePlaceSheetAndTheSheetCarriesTheMove)
  {
    // The map drew nothing at all for a parked fleet before ADR-079, so no tap in this pane could
    // reach a move on a board with nothing in transit -- which is every board at tick zero. Since
    // ADR-112 the badge opens the PLACE, and the move is a control on it.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Assert::IsTrue(WhereTheFleetStands(page.State()) != Lockstep::EventRefs::NONE, L"the opening board has no fleet standing anywhere");
    Assert::IsFalse(
      std::any_of(page.State().fleets.begin(), page.State().fleets.end(), [](const Lockstep::Fleet& _fleet) { return _fleet.OnALane(); }),
      L"something is in transit, so a marker rather than a badge could be what answers this");

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, MAP_LEFT, TOP_BAR, MAP_RIGHT, SCREEN_HEIGHT, [&page]
                                 { return page.OpenPanel() == Lockstep::MainPage::Panel::Place && !FleetControlsOn(page).empty(); });
    Assert::IsTrue(opened, L"nothing on the map opens a place sheet carrying the fleet standing there");

    // And the control on it leads on to the picker, which is the only thing it is for.
    const std::vector<Lockstep::MainPage::HitRegion> controls = FleetControlsOn(page);
    (void)page.HandleTap(controls.front().x + controls.front().width * 0.5F, controls.front().y + controls.front().height * 0.5F);
    Assert::IsTrue(page.MoveOrder().has_value(), L"the sheet's fleet control took the move nowhere");
  }

  TEST_METHOD(ASystemHoldingSeveralListsEveryOneOfThem)
  {
    // A badge totals SHIPS, so a system holding two fleets wears one badge and the sheet behind it
    // has to carry both -- a list that picked one for you would be picking the wrong one half the
    // time (ADR-079, ADR-112).
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    const std::int32_t standing = WhereTheFleetStands(state);
    Assert::IsTrue(standing != Lockstep::EventRefs::NONE);

    Lockstep::Fleet second = state.fleets.front();
    second.id = 77;
    second.name = "FLT 77";
    second.ships = 4;
    second.from = standing;
    second.to = standing;
    second.order = Lockstep::FleetStance::Hold;
    second.underWay = false;
    state.fleets.push_back(std::move(second));

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    const bool listed = SweepFor(page, renderers, DrawPage, MAP_LEFT, TOP_BAR, MAP_RIGHT, SCREEN_HEIGHT, [&page]
                                 { return page.OpenPanel() == Lockstep::MainPage::Panel::Place && FleetControlsOn(page).size() >= 2; });
    Assert::IsTrue(listed, L"a system holding two of your fleets opened a sheet carrying fewer than two of them");

    // Two controls about two different fleets, rather than one fleet offered twice.
    const std::vector<Lockstep::MainPage::HitRegion> controls = FleetControlsOn(page);
    Assert::AreNotEqual(controls[0].index, controls[1].index, L"the sheet offered one fleet twice");
  }

  TEST_METHOD(ARivalsGarrisonIsReadAndNotOrdered)
  {
    // A rival's badge says how strong a system is, which is the same fact ADR-063 puts on a
    // destination row. It is not a control: there is no order to give about somebody else's ships.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsFalse(state.fleets.empty());

    // Every fleet on the board belongs to a rival, so any picker or take-back this sweep finds is
    // one the screen offered about ships that are not the viewer's.
    for (Lockstep::Fleet& fleet : state.fleets)
    {
      fleet.owner = state.viewer == 0 ? 1 : 0;
      fleet.order = Lockstep::FleetStance::Hold;
      fleet.underWay = false;
      fleet.from = fleet.to;
    }

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.MoveOrder().has_value() || !FleetControlsOn(page).empty(); });
    Assert::IsFalse(opened, L"the screen offered an order about a rival's ships");
  }

  TEST_METHOD(ABadgeOpensAnInertSheetAtTheLock)
  {
    // Screen 06, and the rule every sheet already follows (ADR-065): at the lock a sheet stays open
    // and goes inert rather than being taken away. **The badge and the disc agree about this since
    // ADR-112**, where the badge went focus-only and the disc opened a sheet -- they were two rules
    // because they opened two different things, and they open one sheet now.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.orders.locked = true;

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, MAP_LEFT, TOP_BAR, MAP_RIGHT, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Place; });
    Assert::IsTrue(opened, L"nothing on the map opened a place sheet at the lock");
    Assert::IsTrue(page.FocusedSystem() != Lockstep::EventRefs::NONE, L"and it focused nothing either, so the tap did nothing at all");

    // Open, and with nothing on it to press: every control a sheet draws passes `EventRefs::NONE`
    // while the orders are locked, so there is no hit to find rather than a hit that refuses.
    Assert::IsTrue(FleetControlsOn(page).empty(), L"a locked place sheet offered a move");
    const bool queued = SweepFor(page, renderers, DrawPage, MAP_LEFT, TOP_BAR, MAP_RIGHT, SCREEN_HEIGHT,
                                 [&page] { return !page.State().orders.queuedBuilds.empty(); });
    Assert::IsFalse(queued, L"a locked place sheet took a build");
  }

  TEST_METHOD(AFleetOnALaneWearsNoBadge)
  {
    // A fleet is drawn as a marker on its lane or as a badge at a system, never as both -- which is
    // what `Fleet::OnALane` is for. A fleet counted at the system it is LEAVING would be drawn
    // twice and, worse, would offer a picker the lock refuses (ADR-077).
    bool sawOne = false;
    for (std::int32_t ticks = 1; ticks <= 14 && !sawOne; ++ticks)
    {
      const auto simulation = FleetMoveTapTests::EverybodyMoves(ticks);
      const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

      for (const Lockstep::Fleet& fleet : state.fleets)
      {
        if (!fleet.underWay)
        {
          continue;
        }
        sawOne = true;
        Assert::IsTrue(fleet.OnALane(), L"a fleet the server has on a lane says it is standing somewhere");
      }
    }
    Assert::IsTrue(sawOne, L"fourteen ticks of six bots put nothing in transit, so this test proved nothing");
  }
};

// The board is still readable while the link is down, and still gives no order (ADR-085).
// The move is chosen ON the map (ADR-114). What is asserted here is the three things that makes
// true and one screen full of consequences: the mode has doors, lighting a system is not an order,
// and while it is on the map is the only thing on the screen that answers a tap.
TEST_CLASS(MoveModeTapTests)
{
public:
  static constexpr std::int32_t MAP_LEFT = 400;
  static constexpr std::int32_t MAP_RIGHT = SCREEN_WIDTH - ORDERS_RAIL;

  /// A board with exactly one of the viewer's fleets, standing, so the garrison badge under it has
  /// one thing it could mean.
  [[nodiscard]] static Lockstep::MatchState OneFleetStanding()
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsFalse(state.fleets.empty(), L"the opening board has no fleet at all");

    bool kept = false;
    std::vector<Lockstep::Fleet> fleets;
    for (Lockstep::Fleet& fleet : state.fleets)
    {
      if (fleet.owner != state.viewer)
      {
        fleets.push_back(fleet);
        continue;
      }
      if (kept)
      {
        continue;
      }
      fleet.order = Lockstep::FleetStance::Hold;
      fleet.underWay = false;
      fleet.to = fleet.from;
      fleets.push_back(fleet);
      kept = true;
    }
    Assert::IsTrue(kept, L"the viewer owns no fleet on the opening board");
    state.fleets = std::move(fleets);
    return state;
  }

  /// Enters the move mode from the map, by pressing the control that starts one in the middle of
  /// the rectangle `AddHit` recorded for it.
  ///
  /// **A blind sweep cannot find it, because the board moves under one.** A tap on a system centres
  /// the camera on that system (ADR-115), so a tap that lands on a disc or a badge lays the whole
  /// map out again -- and a grid walk stepping eight pixels between taps is then searching a board
  /// that is not the one it measured. Pressing a recorded rectangle is what `OpenPlaceSheet` does,
  /// and it still finds a control by WHAT IT DOES rather than by where it is, which is the property
  /// `Headless.h` exists to keep. The list is re-read every attempt for the same reason: it belongs
  /// to the frame it was built in.
  ///
  /// `_through` names which door, because which one is used is part of what some of these claim.
  [[nodiscard]] static bool EnterMoveThrough(Lockstep::MainPage& _page, Headless& _renderers, Lockstep::MainPage::Action _through)
  {
    std::vector<std::int32_t> tried;
    while (tried.size() < 32)
    {
      _renderers.Begin();
      DrawPage(_page, _renderers);

      const auto candidate =
        std::ranges::find_if(_page.Hits(),
                             [&tried, _through](const Lockstep::MainPage::HitRegion& _hit)
                             {
                               return _hit.action == _through && _hit.index >= 0 && _hit.x >= static_cast<float>(MAP_LEFT) &&
                                      _hit.x < static_cast<float>(MAP_RIGHT) && std::ranges::find(tried, _hit.index) == tried.end();
                             });
      if (candidate == _page.Hits().end())
      {
        return false;
      }

      tried.push_back(candidate->index);
      (void)_page.HandleTap(candidate->x + candidate->width * 0.5F, candidate->y + candidate->height * 0.5F);
      if (_page.MoveOrder().has_value())
      {
        return true;
      }
    }
    return false;
  }

  /// The mode, through whichever of the map's two doors offers it: the garrison badge over a single
  /// standing fleet, or the fleet's own marker (ADR-079, ADR-114).
  [[nodiscard]] static bool EnterAMoveFromTheMap(Lockstep::MainPage& _page, Headless& _renderers)
  {
    return EnterMoveThrough(_page, _renderers, Lockstep::MainPage::Action::OpenFleetsAt) ||
           EnterMoveThrough(_page, _renderers, Lockstep::MainPage::Action::BeginMove);
  }

  TEST_METHOD(AGarrisonBadgeWithOneFleetUnderItSkipsTheSheet)
  {
    // **A badge totals SHIPS** (ADR-079), so a place holding one of your fleets has exactly one
    // thing a tap on it could mean, and a sheet between the finger and the map would be a tap spent
    // on a question with one answer.
    Lockstep::MainPage page;
    page.Create(OneFleetStanding());

    Headless renderers;
    const bool onTheMap = EnterMoveThrough(page, renderers, Lockstep::MainPage::Action::OpenFleetsAt);
    Assert::IsTrue(onTheMap, L"nothing on the map takes the one standing fleet's move onto it");
    Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::None, L"the mode left a sheet open over the map it is played on");
  }

  TEST_METHOD(TheMapOffersTheSystemsOneLaneAwayAndNothingElse)
  {
    // **One lane and no further, because that is what the rules allow.** `Match::Validate` refuses
    // any destination that is not one lane from where the fleet stands, so a lit system further off
    // would be a system the lock is certain to refuse -- the thing ADR-053 took off this screen.
    Lockstep::MainPage page;
    page.Create(OneFleetStanding());

    Headless renderers;
    const bool onTheMap = EnterAMoveFromTheMap(page, renderers);
    Assert::IsTrue(onTheMap, L"no move to check the reach of");

    renderers.Begin();
    DrawPage(page, renderers);

    // Read through a guard rather than through the assertion above it: a `std::optional` dereference
    // that is only safe because a test framework would have thrown is one the linter cannot see.
    const std::optional<Lockstep::MainPage::MoveMode>& move = page.MoveOrder();
    Assert::IsTrue(move.has_value(), L"the move went away between the sweep and the audit");
    const std::int32_t origin = move.has_value() ? move->origin : Lockstep::EventRefs::NONE;

    std::vector<std::int32_t> adjacent;
    for (const Lockstep::Lane& lane : page.State().graph.lanes)
    {
      const std::int32_t other = lane.a == origin ? lane.b : (lane.b == origin ? lane.a : Lockstep::EventRefs::NONE);
      if (other != Lockstep::EventRefs::NONE &&
          !Lockstep::HasFlag(page.State().graph.systems[static_cast<std::size_t>(other)].flags, Lockstep::SystemFlags::RegionAnchor) &&
          std::ranges::find(adjacent, other) == adjacent.end())
      {
        adjacent.push_back(other);
      }
    }
    Assert::IsFalse(adjacent.empty(), L"the fleet's system has no lanes, so this test proves nothing");

    std::vector<std::int32_t> offered;
    for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
    {
      if (hit.action == Lockstep::MainPage::Action::ChooseDestination && std::ranges::find(offered, hit.index) == offered.end())
      {
        offered.push_back(hit.index);
      }
    }
    std::ranges::sort(adjacent);
    std::ranges::sort(offered);
    Assert::IsTrue(adjacent == offered, L"the map offered something other than exactly the systems one lane away");
  }

  TEST_METHOD(EverythingElseOnTheScreenStopsAnsweringATap)
  {
    // **The map is what the question is about, so the map is what answers** (ADR-114). The digest
    // fades and records nothing, the map's own discs and badges record nothing, and the only hits
    // left are the destinations, the strip and the two ways out.
    Lockstep::MainPage page;
    page.Create(OneFleetStanding());

    Headless renderers;
    const bool onTheMap = EnterAMoveFromTheMap(page, renderers);
    Assert::IsTrue(onTheMap, L"no move to check the screen around");

    renderers.Begin();
    DrawPage(page, renderers);

    for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
    {
      Assert::IsTrue(hit.x >= Lockstep::Frame::DIGEST_WIDTH, L"the digest column is still a target while the map is taking a move");

      // **The locks rail keeps its links**, and that is deliberate: it is a list of what goes in at
      // the lock, changing your mind about which place you are looking at is a thing to be able to
      // do, and following one of its rows leaves the mode rather than opening a sheet behind the
      // strip. What must offer nothing but the move is the MAP PANE, which is where the question is.
      if (hit.x >= SCREEN_WIDTH - ORDERS_RAIL)
      {
        continue;
      }

      const bool allowed = hit.action == Lockstep::MainPage::Action::ChooseDestination ||
                           hit.action == Lockstep::MainPage::Action::SendMove || hit.action == Lockstep::MainPage::Action::CancelMove ||
                           hit.action == Lockstep::MainPage::Action::None;
      Assert::IsTrue(allowed, L"something other than the move answered a tap over the map while the mode was on");
    }

    // And a rail row does leave the mode rather than opening a sheet under the strip.
    const auto link =
      std::ranges::find_if(page.Hits(), [](const Lockstep::MainPage::HitRegion& _hit)
                           { return _hit.action == Lockstep::MainPage::Action::OpenSystem && _hit.x >= SCREEN_WIDTH - ORDERS_RAIL; });
    if (link != page.Hits().end())
    {
      (void)page.HandleTap(link->x + link->width * 0.5F, link->y + link->height * 0.5F);
      Assert::IsFalse(page.MoveOrder().has_value(), L"a rail row opened a sheet behind the confirm strip");
      Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::Place, L"and it opened nothing at all");
    }
  }

  TEST_METHOD(EscapeLeavesTheModeAndSoDoesTheBoard)
  {
    // Two of the four ways out the banner names. The other two are its own `CANCEL` and the strip's,
    // which are ordinary hits and are swept for above.
    Lockstep::MainPage page;
    page.Create(OneFleetStanding());

    Headless renderers;
    Assert::IsTrue(EnterAMoveFromTheMap(page, renderers), L"no move to leave");

    Assert::IsTrue(page.HandleKey(Neuron::KeyboardInput::Key::Escape), L"ESC did not leave the move mode");
    Assert::IsFalse(page.MoveOrder().has_value(), L"and the mode is still on");

    // Back on, and out again by tapping a part of the board that offers nothing. A tap that hits
    // nothing is what the empty map, an unreachable system and a rival's garrison all are.
    Assert::IsTrue(EnterAMoveFromTheMap(page, renderers), L"the mode would not come back");
    renderers.Begin();
    DrawPage(page, renderers);

    // A point inside the map pane that no hit covers. Found rather than chosen, so the test does
    // not carry a coordinate that the next layout change makes a lie.
    bool left = false;
    for (std::int32_t y = TOP_BAR + 8; y < SCREEN_HEIGHT && !left; y += STEP)
    {
      for (std::int32_t x = MAP_LEFT; x < MAP_RIGHT && !left; x += STEP)
      {
        const bool covered = std::ranges::any_of(page.Hits(),
                                                 [x, y](const Lockstep::MainPage::HitRegion& _hit)
                                                 {
                                                   return static_cast<float>(x) >= _hit.x && static_cast<float>(x) < _hit.x + _hit.width &&
                                                          static_cast<float>(y) >= _hit.y && static_cast<float>(y) < _hit.y + _hit.height;
                                                 });
        if (covered)
        {
          continue;
        }
        Assert::IsTrue(page.HandleTap(static_cast<float>(x), static_cast<float>(y)), L"a tap on the bare board was not even handled");
        left = !page.MoveOrder().has_value();
      }
    }
    Assert::IsTrue(left, L"a tap on the bare board did not leave the mode");
  }

  TEST_METHOD(NothingMovesOnItsOwnWhileTheClockIsHeld)
  {
    // **`--still` is what makes a capture of this mode reproducible** (ADR-114). The ring breathes
    // and the lanes march, both as pure functions of one clock, so holding the clock is the whole
    // of freezing them -- and a page that asks for a frame every frame is a page a capture never
    // catches at rest.
    Lockstep::MainPage page;
    page.Create(OneFleetStanding());

    Headless renderers;
    Assert::IsTrue(EnterAMoveFromTheMap(page, renderers), L"no move to freeze");
    Assert::IsTrue(page.Animating(), L"a mode with a pulsing ring in it does not ask to be redrawn");

    page.SetStill(true);
    Assert::IsFalse(page.Animating(), L"--still left the page asking for a frame every frame");

    // And the clock does not move under it, so two frames a second apart are the same frame -- which
    // is measured rather than argued, because the pulse is an alpha and nothing else about the
    // screen would have changed either way.
    renderers.Begin();
    DrawPage(page, renderers);
    const std::uint64_t before = ShapeFingerprint(renderers.shapes);
    page.Update(1.0);
    renderers.Begin();
    DrawPage(page, renderers);
    Assert::AreEqual(before, ShapeFingerprint(renderers.shapes), L"--still drew a different frame a second later");

    // And without it, it does: the guard above is holding something that genuinely moves.
    page.SetStill(false);
    renderers.Begin();
    DrawPage(page, renderers);
    const std::uint64_t moving = ShapeFingerprint(renderers.shapes);
    page.Update(0.4);
    renderers.Begin();
    DrawPage(page, renderers);
    Assert::AreNotEqual(moving, ShapeFingerprint(renderers.shapes), L"the ring does not actually move, so freezing it proves nothing");
  }

  TEST_METHOD(TheDigestIsDrawnFadedAndNotJustMadeDeaf)
  {
    // **The fade is half of the rule and the hit list is the other half** (ADR-114). A column that
    // records nothing and still draws its controls at full strength is a player being shown things
    // they cannot press, so this reads the VERTICES rather than the hits.
    //
    // It is asserted on ONE named ink rather than by differencing two frames, because the fade
    // multiplies alpha by 0.55 and two different washes can land on one value -- "this colour is in
    // both frames" is not evidence of anything. `Ink::OUTLINE` is the border of an `Outlined`
    // control, which is what every digest button becomes while the mode is on, and nothing else on
    // this column draws it: if it survives, a button drew it.
    //
    // This is the defect it was written for. Every band, rule and label on the column went through
    // `Faded` and the BUTTONS did not, because a button's ink is a bundle that was passed on whole.
    const auto inksIn = [](Neuron::ShapeRenderer& _shapes)
    {
      std::set<std::uint32_t> found;
      for (std::int32_t drain = 0; drain < 4; ++drain)
      {
        const Neuron::ShapeRenderer::Batch batch = _shapes.TakeUnflushed();
        for (const Neuron::ShapeRenderer::ShapeVertex& vertex : batch.vertices)
        {
          if (vertex.positionXPixels < Lockstep::Frame::DIGEST_WIDTH)
          {
            found.insert(vertex.color);
          }
        }
      }
      return found;
    };

    Lockstep::MainPage page;
    page.Create(OneFleetStanding());

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);
    Assert::IsTrue(inksIn(renderers.shapes).contains(Neuron::Pack(Lockstep::Ink::OUTLINE)),
                   L"the resting digest draws no outlined control, so this test is measuring nothing");

    const bool onTheMap = EnterAMoveFromTheMap(page, renderers);
    Assert::IsTrue(onTheMap, L"no move to fade the column under");

    renderers.Begin();
    DrawPage(page, renderers);
    const std::set<std::uint32_t> faded = inksIn(renderers.shapes);
    Assert::IsFalse(faded.contains(Neuron::Pack(Lockstep::Ink::OUTLINE)),
                    L"a digest button kept its full-strength border while the map was taking a move");

    // And it is faded rather than simply gone: 51 x 0.55 rounds to 28, and a column that stopped
    // drawing its buttons would pass the assertion above for the wrong reason.
    Assert::IsTrue(faded.contains(Neuron::Pack(Neuron::Color{255, 255, 255, 28})), L"the buttons did not fade, they vanished");

    // Nothing on the column is filled while the mode is on, which is the ADR-089 half of the same
    // rule and is reached a second way: a digest button never takes the `Primary` state at all
    // while `m_moveMode` holds one, so the strip's `SEND` is the only filled control on the screen.
    Assert::IsFalse(faded.contains(Neuron::Pack(Lockstep::Ink::BLUE)), L"the digest still has a filled blue control on it");
  }
};

// The map has a camera the player can move, and a way back to where it started (ADR-090).
TEST_CLASS(MapCameraTapTests)
{
public:
  /// The pane the map is drawn into, which is what "the middle of the map" means below.
  static constexpr float PANE_X = Lockstep::Frame::DIGEST_WIDTH;
  static constexpr float PANE_WIDTH = Lockstep::Frame::SCREEN_WIDTH - Lockstep::Frame::DIGEST_WIDTH - Lockstep::Frame::ORDERS_WIDTH;
  static constexpr float PANE_HEIGHT = Lockstep::Frame::SCREEN_HEIGHT - Lockstep::Frame::TOP_BAR_HEIGHT;

  /// Taps the first thing on the map that opens a system, and reports which one it focused.
  ///
  /// It presses rather than calling a focus method, for the reason every test in this file does:
  /// what is under test is what a TAP does, and a focus reached any other way is not one.
  [[nodiscard]] static std::int32_t TapASystem(Lockstep::MainPage& _page, Headless& _renderers)
  {
    _renderers.Begin();
    DrawPage(_page, _renderers);

    // A COPY, because the redraw after the tap rebuilds the list being walked.
    const std::vector<Lockstep::MainPage::HitRegion> candidates = _page.Hits();
    for (const Lockstep::MainPage::HitRegion& hit : candidates)
    {
      if (hit.action != Lockstep::MainPage::Action::OpenSystem || hit.index < 0)
      {
        continue;
      }
      (void)_page.HandleTap(hit.x + hit.width * 0.5F, hit.y + hit.height * 0.5F);
      return _page.FocusedSystem();
    }
    return Lockstep::EventRefs::NONE;
  }

  /// Where a system's ground point lands on the screen, through the camera as the last frame left
  /// it. The page must have been drawn since the tap: `FrameContent` is what applies the aim, and
  /// it runs when the map is drawn rather than when the tap is handled (ADR-115).
  [[nodiscard]] static Neuron::OrbitCamera::ScreenPoint WhereOnScreen(const Lockstep::MainPage& _page, std::int32_t _system)
  {
    const Lockstep::SystemNode& node = _page.State().graph.systems[static_cast<std::size_t>(_system)];
    return _page.Map().Camera().Project(Lockstep::MapView::Ground(node.positionX, node.positionY));
  }

  TEST_METHOD(AWheelOverTheMapMovesTheCameraAndStopsAtTheEnds)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    Assert::IsTrue(page.Map().AtAuthoredFraming(), L"the map does not open at the authored framing");

    Assert::IsTrue(page.HandleZoom(1, 700.0F, 300.0F), L"a notch over the map moved nothing");
    Assert::IsFalse(page.Map().AtAuthoredFraming());

    // Spun hard against the stop it reports no change, so an idle frame costs no redraw (ADR-047).
    for (std::int32_t again = 0; again < 40; ++again)
    {
      (void)page.HandleZoom(1, 700.0F, 300.0F);
    }
    Assert::IsFalse(page.HandleZoom(1, 700.0F, 300.0F), L"the zoom has no near limit");

    for (std::int32_t back = 0; back < 80; ++back)
    {
      (void)page.HandleZoom(-1, 700.0F, 300.0F);
    }
    Assert::IsFalse(page.HandleZoom(-1, 700.0F, 300.0F), L"the zoom has no far limit");
  }

  TEST_METHOD(ANotchOverTheRailIsNeitherAListNorACamera)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Assert::IsFalse(page.HandleZoom(1, 1150.0F, 300.0F), L"a notch over the locks rail did something");
    Assert::IsTrue(page.Map().AtAuthoredFraming());
    Assert::AreEqual(std::size_t{0}, page.DigestTop());
  }

  TEST_METHOD(ResetIsOnTheScreenOnlyWhenThereIsSomethingToReset)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    // At the authored framing there is no chip, so no tap anywhere on the map pane finds one -- a
    // control that would do nothing is left off the screen rather than drawn dim.
    Headless renderers;
    Assert::IsTrue(page.Map().AtAuthoredFraming());

    // Move the camera, then sweep the top of the map pane for the chip that takes it back.
    (void)page.HandleZoom(2, 700.0F, 300.0F);
    Assert::IsFalse(page.Map().AtAuthoredFraming());

    const bool reset = SweepFor(page, renderers, DrawPage, 400, TOP_BAR, SCREEN_WIDTH - ORDERS_RAIL, TOP_BAR + 40,
                                [&page] { return page.Map().AtAuthoredFraming(); });
    Assert::IsTrue(reset, L"nothing on the map pane puts the camera back");
  }

  TEST_METHOD(ADragStillOrbitsAndResetUndoesThatToo)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    const Neuron::PointerInput::Drag turn{.deltaXPixels = 40.0F, .deltaYPixels = 0.0F, .originXPixels = 700.0F, .originYPixels = 300.0F};
    Assert::IsTrue(page.HandleDrag(turn), L"a drag that began on the map was not consumed by it");
    Assert::IsFalse(page.Map().AtAuthoredFraming(), L"the drag did not orbit anything");

    page.ResetView();
    Assert::IsTrue(page.Map().AtAuthoredFraming(), L"reset left the camera somewhere else");
  }

  // **A tap on a system puts that system in the middle of the map** (ADR-115), which is a claim
  // about the camera rather than about the ring and the caption a focus also draws: it is asserted
  // by projecting the system's own ground point and asking where on the pane it landed.
  TEST_METHOD(ATapOnASystemCentersTheCameraOnIt)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    const std::int32_t system = TapASystem(page, renderers);
    Assert::AreNotEqual(Lockstep::EventRefs::NONE, system, L"nothing on the map focused a system");

    renderers.Begin();
    DrawPage(page, renderers);

    // Lifted when the same tap opened a sheet over the bottom of the pane, and dead centre when it
    // did not: what is centred is the map that can still be seen.
    const float lift = page.OpenPanel() == Lockstep::MainPage::Panel::None ? 0.0F : Lockstep::MainPage::FOCUS_LIFT_PIXELS;

    const Neuron::OrbitCamera::ScreenPoint where = WhereOnScreen(page, system);
    Assert::IsTrue(where.visible, L"the system the camera was aimed at does not project");
    Assert::AreEqual(PANE_X + PANE_WIDTH * 0.5F, where.xPixels, 0.05F, L"the tapped system is not in the middle across");
    Assert::AreEqual(Lockstep::Frame::TOP_BAR_HEIGHT + PANE_HEIGHT * 0.5F - lift, where.yPixels, 0.05F,
                     L"the tapped system is not in the middle of the map that is showing");
  }

  // The tap that centres a system is also the tap that opens a sheet over the bottom of the pane
  // (ADR-112), so the centring has to clear it -- a camera aimed at something behind a sheet has
  // moved for nothing.
  TEST_METHOD(ACenteredSystemIsNotLeftUnderTheSheet)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    Assert::IsTrue(OpenPlaceSheet(page, renderers), L"nothing on the map opened a place sheet");

    const std::int32_t system = page.FocusedSystem();
    Assert::AreNotEqual(Lockstep::EventRefs::NONE, system, L"the sheet opened about nothing");

    const Lockstep::Frame::Box sheet = SheetBoundsOf(page);
    Assert::IsTrue(sheet.height > 0.0F, L"the open sheet has no bounds to clear");

    const Neuron::OrbitCamera::ScreenPoint where = WhereOnScreen(page, system);
    Assert::IsTrue(where.visible);
    Assert::IsTrue(where.yPixels < sheet.y, L"the sheet covers the system the camera was just aimed at");
    Assert::IsTrue(where.yPixels > Lockstep::Frame::TOP_BAR_HEIGHT, L"the lift pushed the system off the top of the pane");
  }

  // **A snapshot takes the centring with it** (ADR-057): a position is only a system while the
  // snapshot it came from is the current one, so a camera left aimed at the tenth system would be
  // aimed at a different place a tick later.
  TEST_METHOD(ATickFramesTheWholeGalaxyAgain)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    Assert::AreNotEqual(Lockstep::EventRefs::NONE, TapASystem(page, renderers), L"nothing on the map focused a system");
    Assert::IsFalse(page.Map().AtAuthoredFraming(), L"a camera centred on a system reports itself where the map opened");

    page.Create(ViewOfSeatZero(*simulation));
    Assert::IsTrue(page.Map().AtAuthoredFraming(), L"the tick left the camera on a position from the last one");
    Assert::AreEqual(Lockstep::EventRefs::NONE, page.FocusedSystem());
  }

  // And there is a way back, which is the same one the zoom and the orbit have: the chip is drawn
  // because the camera is no longer where the map opened, and pressing it frames the galaxy again
  // (ADR-090).
  TEST_METHOD(ResetComesBackFromACenteringToo)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    Assert::AreNotEqual(Lockstep::EventRefs::NONE, TapASystem(page, renderers), L"nothing on the map focused a system");
    Assert::IsFalse(page.Map().AtAuthoredFraming());

    const bool reset = SweepFor(page, renderers, DrawPage, 400, TOP_BAR, SCREEN_WIDTH - ORDERS_RAIL, TOP_BAR + 40,
                                [&page] { return page.Map().AtAuthoredFraming(); });
    Assert::IsTrue(reset, L"nothing on the map pane takes a centring back");
    Assert::AreNotEqual(Lockstep::EventRefs::NONE, page.FocusedSystem(), L"reset is about the camera and cleared the focus too");
  }
};

// The destination sheet is sorted by how soon a fleet lands, and the digest admits a clipped
// backlog (ADR-092, ADR-094).
TEST_CLASS(SheetOrderAndBacklogTests)
{
public:
  TEST_METHOD(TheClientsIdeaOfTheBacklogWindowIsEightTicks)
  {
    // **The two constants cannot be compared from any one test project**, which is worth saying
    // rather than working around: this suite links `LockstepClient`, `GameLogic` and `NeuronCore`,
    // and `NeuronServer::Session::DIGEST_HISTORY` is in none of them (AGENTS.md section 2). So this
    // pins the client's half and names the other, and a session that changes the server's window
    // finds this test by grepping for the name.
    Assert::AreEqual(8U, Lockstep::MainPage::DIGEST_HISTORY_TICKS,
                     L"the client's backlog window moved; NeuronServer::Session::DIGEST_HISTORY must match it");
  }

  TEST_METHOD(TheConfirmStripPutsTheNearestFirst)
  {
    // Lane order is the order the graph happens to store them in and means nothing to a player; how
    // soon a fleet lands is the first thing they weigh (ADR-092). The sort moved onto the strip with
    // the list (ADR-114), and it is asserted off `ReachableFor` rather than off the drawing --
    // `MoveTargetSystem` carries the two numbers the rows are sorted by.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    const bool onTheMap =
      SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT, [&page] { return page.MoveOrder().has_value(); });
    Assert::IsTrue(onTheMap, L"no move to check the order of");

    // The rows on the strip and the chips on the map are one list, drawn twice, and the hit list is
    // where that can be read back: the destinations in the order the strip laid them out.
    renderers.Begin();
    DrawPage(page, renderers);

    std::vector<std::uint32_t> ticks;
    const std::optional<Lockstep::MainPage::MoveMode>& move = page.MoveOrder();
    Assert::IsTrue(move.has_value(), L"the move went away between the sweep and the audit");
    const std::int32_t origin = move.has_value() ? move->origin : Lockstep::EventRefs::NONE;
    for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
    {
      if (hit.action != Lockstep::MainPage::Action::ChooseDestination || hit.y < Lockstep::Frame::SCREEN_HEIGHT * 0.5F)
      {
        continue;
      }
      for (const Lockstep::Lane& lane : page.State().graph.lanes)
      {
        const bool joins = (lane.a == origin && lane.b == hit.index) || (lane.b == origin && lane.a == hit.index);
        if (joins)
        {
          ticks.push_back(lane.cost);
        }
      }
    }
    Assert::IsFalse(ticks.empty(), L"the strip offered no destination, so there is nothing to sort");
    Assert::IsTrue(std::ranges::is_sorted(ticks), L"the strip's rows are not in arrival order");
  }
};

// The stations are balls now, recorded into a third recorder and lit by the shader between three
// authored tones (ADR-103, ADR-104). What is pinned here is the half of that a screenshot cannot
// say: that every ball on a played board was recorded, whole, with its ramp running dark, lit, rim
// -- ADR-012's static_assert, made against a board rather than a header -- and that the page
// records its shapes in two layers around them, so the rings and badges land over the balls.
TEST_CLASS(StationBallTests)
{
public:
  [[nodiscard]] static Neuron::Color Unpack(std::uint32_t _packed)
  {
    return Neuron::Color{static_cast<std::uint8_t>(_packed & 0xFFU), static_cast<std::uint8_t>((_packed >> 8U) & 0xFFU),
                         static_cast<std::uint8_t>((_packed >> 16U) & 0xFFU), static_cast<std::uint8_t>((_packed >> 24U) & 0xFFU)};
  }

  TEST_METHOD(EveryStationIsASolidWhoseFourTonesRunInOrder)
  {
    Headless renderers;
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*PlayedMatch(6)));
    renderers.Begin();
    DrawPage(page, renderers);

    const auto vertices = renderers.meshes.Vertices();
    Assert::IsFalse(vertices.empty(), L"a board with systems on it recorded no balls");
    Assert::AreEqual(static_cast<std::size_t>(0), vertices.size() % 3, L"a ball is whole triangles");

    std::size_t stations = 0;
    for (const Lockstep::SystemNode& node : page.State().graph.systems)
    {
      if (!Lockstep::HasFlag(node.flags, Lockstep::SystemFlags::RegionAnchor))
      {
        ++stations;
      }
    }
    Assert::IsTrue(vertices.size() >= stations * (Neuron::MeshRenderer::SphereVertexCount(8) + Neuron::MeshRenderer::COLUMN_VERTEX_COUNT),
                   L"fewer solids than stations on the board");

    for (const Neuron::MeshRenderer::MeshVertex& vertex : vertices)
    {
      // The ramp runs shadow, past the terminator, grazed, lit, silhouette (ADR-105, ADR-108), and
      // every step of it has to climb: two bands the wrong way round on a small ball read as a
      // modelling error rather than as a palette one.
      Assert::IsTrue(Neuron::Luminance(Unpack(vertex.quarterLitColor)) > Neuron::Luminance(Unpack(vertex.darkColor)),
                     L"a ball is darker just past its terminator than in its shadow");
      Assert::IsTrue(Neuron::Luminance(Unpack(vertex.halfLitColor)) > Neuron::Luminance(Unpack(vertex.quarterLitColor)),
                     L"a ball is darker where the light grazes it than just past its terminator");
      Assert::IsTrue(Neuron::Luminance(Unpack(vertex.litColor)) > Neuron::Luminance(Unpack(vertex.halfLitColor)),
                     L"a ball is darker where the light finds it than where it grazes it");
      // The silhouette tone is either brighter than the lit face -- a ball, where the limb catches
      // the light the surface curves past -- or exactly the shadow, which is how a FLAT-faced solid
      // switches the rim off (`Ink::FlatRampFor`). A stem is the second kind, and anything between
      // the two would be a tone nobody chose for either.
      const bool rimLifts = Neuron::Luminance(Unpack(vertex.rimColor)) > Neuron::Luminance(Unpack(vertex.litColor));
      const bool rimOff = Unpack(vertex.rimColor).red == Unpack(vertex.darkColor).red &&
                          Unpack(vertex.rimColor).green == Unpack(vertex.darkColor).green &&
                          Unpack(vertex.rimColor).blue == Unpack(vertex.darkColor).blue;
      Assert::IsTrue(rimLifts || rimOff, L"a silhouette tone that neither lifts nor is switched off");
      Assert::AreEqual(Neuron::OPAQUE_ALPHA, Unpack(vertex.litColor).alpha, L"the mesh pass does not blend, so a tone is opaque");
      Assert::AreEqual(Neuron::OPAQUE_ALPHA, Unpack(vertex.halfLitColor).alpha);
      Assert::AreEqual(Neuron::OPAQUE_ALPHA, Unpack(vertex.quarterLitColor).alpha);
      Assert::AreEqual(Neuron::OPAQUE_ALPHA, Unpack(vertex.darkColor).alpha);
      Assert::AreEqual(Neuron::OPAQUE_ALPHA, Unpack(vertex.rimColor).alpha);

      // The glint is either brighter than the lit face -- a ball -- or exactly it, which is how a
      // flat-faced solid opts out (`Ink::FlatRampFor`). Anything between would be a tone nobody
      // chose for either (ADR-106).
      const bool glintLifts = Neuron::Luminance(Unpack(vertex.glintColor)) > Neuron::Luminance(Unpack(vertex.litColor));
      const bool glintOff = Unpack(vertex.glintColor).red == Unpack(vertex.litColor).red &&
                            Unpack(vertex.glintColor).green == Unpack(vertex.litColor).green &&
                            Unpack(vertex.glintColor).blue == Unpack(vertex.litColor).blue;
      Assert::IsTrue(glintLifts || glintOff, L"a glint tone that neither lifts nor is switched off");
    }
  }

  TEST_METHOD(TheWorldIsRecordedInTwoShapeLayersAroundTheBalls)
  {
    Headless renderers;
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*PlayedMatch(6)));
    renderers.Begin();
    page.DrawWorld(renderers.shapes, renderers.text, renderers.meshes);

    const Neuron::ShapeRenderer::Batch ground = renderers.shapes.TakeUnflushed();
    const Neuron::ShapeRenderer::Batch over = renderers.shapes.TakeUnflushed();
    Assert::IsFalse(ground.vertices.empty(), L"nothing under the balls: no ground, no lanes, no stems");
    Assert::IsFalse(over.vertices.empty(), L"nothing over the balls: the rings and badges would be drawn under them");
    Assert::IsTrue(renderers.shapes.TakeUnflushed().vertices.empty(), L"the world is exactly two shape layers");
  }
};

} // namespace LockstepTests
