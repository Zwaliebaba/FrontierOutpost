// PlaceSheetTapTests.cpp -- the place sheet, tapped: opening it, the build queue and its tiles
// (ADR-107), the sheet itself (ADR-112), and the purse it prices against (ADR-078).

#include "pch.h"
#include "CppUnitTest.h"

#include "Headless.h"

#include "MainPage.h"
#include "SnapshotView.h"

#include "TickResolver.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

// A sheet is where somebody is in the middle of deciding something, and neither the lock nor the
// state that follows it takes that away (ADR-065).
TEST_CLASS(OpenSheetTapTests)
{
public:
  TEST_METHOD(ASheetSurvivesTheLockAndStillTakesNoOrder)
  {
    // The place sheet, which since ADR-112 is the sheet an order is given on and so the one it
    // matters most that the lock does not take away.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Place; });
    Assert::IsTrue(opened, L"no control opens a place sheet on an opening board");

    // The lock arrives under it.
    page.Update(1.0e6);
    Assert::IsTrue(page.State().orders.locked);
    Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::Place, L"the lock closed the sheet");

    std::vector<std::int32_t> before;
    for (const Lockstep::Fleet& fleet : page.State().fleets)
    {
      before.push_back(fleet.to);
    }

    // **What the sweep above happened to queue on its way to the picker is the baseline, not zero.**
    // It is an unlocked board and the digest offers a build on it, so a sweep that finds the picker
    // may well have queued one first. What this test is about is whether anything can be ordered
    // AFTER the lock, which is a change rather than a count.
    const std::size_t builds = page.State().orders.queuedBuilds.size();
    const std::size_t signals = page.State().orders.queuedSignals.size();

    const bool ordered =
      SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT, [&page, builds, signals]
               { return page.State().orders.queuedBuilds.size() != builds || page.State().orders.queuedSignals.size() != signals; });
    Assert::IsFalse(ordered, L"a sheet left open at the lock took an order");
    for (std::size_t index = 0; index < before.size(); ++index)
    {
      Assert::AreEqual(before[index], page.State().fleets[index].to, L"a locked sheet ordered a move");
    }
  }

  TEST_METHOD(ASheetIsRebuiltFromTheNewStateOrClosed)
  {
    // The subject is remembered as the id the simulation knows it by, not as a position in a fogged
    // list (ADR-057), so a sheet reopens on the same system rather than on whichever one has moved
    // into that slot.
    const auto simulation = PlayedMatch(4);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Place; });
    Assert::IsTrue(opened, L"nothing opens a build sheet on a played board");

    const std::int32_t about = page.FocusedSystem();
    Assert::IsTrue(about >= 0);
    const std::int32_t identity = page.State().graph.systems[static_cast<std::size_t>(about)].id;

    page.Create(page.State());
    Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::Place, L"a new state closed a sheet that still had a subject");

    // Taken by somebody: there is nothing left to build on it, so the sheet goes. That it closes on
    // exactly this condition is what says the subject is tracked rather than merely kept.
    Lockstep::MatchState lost = page.State();
    for (Lockstep::SystemNode& node : lost.graph.systems)
    {
      if (node.id == identity)
      {
        node.owner = lost.viewer + 1;
      }
    }
    page.Create(std::move(lost));
    Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::None, L"a sheet stayed open on a system that is no longer yours");
  }
};

// The price is on the button (ADR-053): a build the purse cannot cover is refused at the tap, by
// the same running-total rule the lock would refuse it by, rather than a tick later in the digest.
TEST_CLASS(BuildQueueTapTests)
{
public:
  TEST_METHOD(ABuildThePurseCoversCanBeQueuedFromTheScreen)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    Assert::IsFalse(page.State().orders.builds.empty(), L"a fresh match offers something to build");
    Assert::IsTrue(page.State().orders.builds.front().cost > 0, L"and every row carries its price");
    Assert::IsTrue(page.State().player.credits >= page.State().orders.builds.front().cost, L"the opening purse covers one building");

    // **It takes two taps now and it used to take one** (ADR-112): no digest control places an
    // order any more, so the first tap opens the place the order is about and the second gives it.
    // The sweep is about the second, and `_ensure` is what keeps the sheet in front of it -- a
    // stray tap on an unclaimed disc closes it, and a sweep hunting for a tile behind a closed
    // sheet would be hunting for something that is not on the screen.
    Headless renderers;
    const bool queued = SweepFor(
      page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT, [&page] { return !page.State().orders.queuedBuilds.empty(); },
      false,
      [&page, &renderers]
      {
        if (page.OpenPanel() == Lockstep::MainPage::Panel::Place)
        {
          return false;
        }
        return OpenPlaceSheet(page, renderers);
      });
    Assert::IsTrue(queued, L"no control on the screen queues a build");
    Assert::AreEqual(std::size_t{1}, Lockstep::OrdersOf(page.State()).builds.size(), L"and it became an order");
  }

  TEST_METHOD(NoDigestControlPlacesAnOrderDirectly)
  {
    // **The sheet is the only door an order goes through** (ADR-112). The digest still carries the
    // controls -- a priced build, a move -- and each of them is a LINK to the place it is about, so
    // a player who taps one lands somewhere they can see what they are spending against rather than
    // committing from a column that shows them neither the purse nor the queue.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    std::size_t links = 0;
    for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
    {
      if (hit.x >= Lockstep::Frame::DIGEST_WIDTH)
      {
        continue;
      }
      const bool order = hit.action == Lockstep::MainPage::Action::ToggleBuild ||
                         hit.action == Lockstep::MainPage::Action::ChooseDestination ||
                         hit.action == Lockstep::MainPage::Action::CancelFleetOrder;
      Assert::IsFalse(order, L"a control in the digest column gives an order rather than opening the place it is about");
      links += hit.action == Lockstep::MainPage::Action::OpenSystem ? 1U : 0U;
    }
    Assert::IsTrue(links > 0, L"the opening digest offers no link to a place, so it offers nothing at all");

    // And the link lands on a sheet about a system the viewer holds, which is the only kind that
    // can take an order (ADR-058).
    for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
    {
      if (hit.action != Lockstep::MainPage::Action::OpenSystem || hit.x >= Lockstep::Frame::DIGEST_WIDTH)
      {
        continue;
      }
      (void)page.HandleTap(hit.x + hit.width * 0.5F, hit.y + hit.height * 0.5F);
      Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::Place, L"a digest link opened no place sheet");
      break;
    }
  }

  // A build row offers the NEXT level, and a system already building offers nothing at all
  // (ADR-069). Both are swept for rather than read off a coordinate, like everything else here.
  TEST_METHOD(ASystemAlreadyBuildingOffersNothingToQueue)
  {
    // Driven through a real order rather than by editing the list: every `EventAction::target` is
    // an index into `orders.builds`, so a test that erases rows after `ViewOf` composed the cards
    // is testing stale indices rather than the rule (ADR-057, one index one meaning).
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    Lockstep::Match match = Lockstep::Match::Create(rules, 0x5349'474E'414C'5321ULL);

    const Lockstep::SystemId capital = match.GalaxyGraph().Capitals()[0];
    Lockstep::OrderSet orders;
    orders.player = Lockstep::PlayerId{0};
    orders.builds.push_back(Lockstep::BuildOrder{.system = capital, .kind = Lockstep::BuildKind::MiningStation});
    const std::vector<Lockstep::OrderSet> sets = {orders};

    Lockstep::TickLog log;
    match = Lockstep::TickResolver::Resolve(match, {.orders = sets}, log);
    Assert::IsTrue(match.SystemAt(capital).construction.Rising(), L"the order started nothing");

    const Lockstep::PlayerId seat{0};
    Lockstep::MatchState state = Lockstep::ViewOf(Lockstep::Snapshot::For(match, seat), Lockstep::Snapshot::DigestFor(log, seat), 600);

    // **`available` and not `!rising`** since ADR-107: a system that is building composes the rows
    // it cannot take yet as well as the one it is taking, so its sheet can show what will be
    // orderable and what it will cost. What must stay true is that none of them is an OFFER, which
    // is exactly what the field says -- and the sweep below is the claim that the screen agrees.
    const bool offersTheRisingSystem =
      std::any_of(state.orders.builds.begin(), state.orders.builds.end(),
                  [capital](const Lockstep::BuildRow& _row) { return _row.system == capital.Index() && _row.available; });
    Assert::IsFalse(offersTheRisingSystem, L"a system already building was offered a second order to queue");

    // And the rows it does compose are there: a sheet with one tile on it has nothing to plan
    // against, which is what this change was for.
    const auto blocked = std::count_if(state.orders.builds.begin(), state.orders.builds.end(), [capital](const Lockstep::BuildRow& _row)
                                       { return _row.system == capital.Index() && !_row.rising; });
    Assert::IsTrue(blocked > 0, L"a system already building composes nothing but the rising row");

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    const bool queued = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page, capital]
                                 {
                                   for (const std::int32_t row : page.State().orders.queuedBuilds)
                                   {
                                     if (page.State().orders.builds[static_cast<std::size_t>(row)].system == capital.Index())
                                     {
                                       return true;
                                     }
                                   }
                                   return false;
                                 });
    Assert::IsFalse(queued, L"a system already building took another order from the screen");
  }

  TEST_METHOD(ABuildRowCarriesItsLevelAndWhatItTakes)
  {
    // The sheet's job is to let a player weigh one level against another, which needs the level,
    // the price and the ticks -- all three from the snapshot, never from a number the client knows
    // (ADR-053).
    const auto simulation = PlayedMatch(0);
    const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    Assert::IsFalse(state.orders.builds.empty());
    for (const Lockstep::BuildRow& row : state.orders.builds)
    {
      Assert::IsTrue(row.level >= 1, L"a build row does not say which level it would build");
      Assert::IsTrue(row.ticks >= 1, L"a build row does not say how long it takes");
      Assert::IsTrue(row.cost > 0, L"a build row does not carry its price");
      Assert::IsFalse(row.detail.empty(), L"a build row does not say what the level buys");
    }
  }

  TEST_METHOD(ABuildThePurseCannotCoverIsNotQueuedByAnyTap)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.player.credits = 0;
    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    const bool queued = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return !page.State().orders.queuedBuilds.empty(); });
    Assert::IsFalse(queued, L"a build the lock would refuse was queued anyway");
    Assert::IsTrue(Lockstep::OrdersOf(page.State()).builds.empty(), L"and nothing went out");
  }

  TEST_METHOD(ARivalsSystemOpensNoBuildSheet)
  {
    // You cannot build on somebody else's ground -- `Match::Validate` refuses it outright -- so a
    // BUILD sheet over a rival's capital lists orders that system cannot take (ADR-058). Tapping
    // one still focuses it, because a tap that does nothing visible is the defect this screen has
    // been bitten by before.
    const auto simulation = PlayedMatch(12);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    // A system on the map that somebody else holds. Twelve ticks of six bots is enough to have met
    // one, and the test says so rather than assuming it.
    const Lockstep::MatchState& state = page.State();
    std::int32_t theirs = Lockstep::EventRefs::NONE;
    for (std::size_t index = 0; index < state.graph.systems.size(); ++index)
    {
      const Lockstep::SystemNode& node = state.graph.systems[index];
      if (node.owner != Lockstep::NOBODY && node.owner != state.viewer)
      {
        theirs = static_cast<std::int32_t>(index);
        break;
      }
    }
    Assert::IsTrue(theirs != Lockstep::EventRefs::NONE, L"twelve ticks met nobody, so this test proved nothing");

    // Swept until the rival's system is the focused one, which is what tapping it does.
    //
    // **The sheet the previous row opened is closed before each tap, and this sweep needs that.**
    // A row that crosses the rail's `SIGNALS` header opens the picker, which covers the map from a
    // third of the way down the pane -- so every later row lands on a sheet rather than on a
    // system, and a system under it can never be reached. `_ensure` is the hook for exactly this.
    Headless renderers;
    const auto closeAnySheet = [&page, &renderers]
    {
      if (page.OpenPanel() == Lockstep::MainPage::Panel::None)
      {
        return false;
      }
      renderers.Begin();
      DrawPage(page, renderers);
      for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
      {
        if (hit.action == Lockstep::MainPage::Action::ClosePanel)
        {
          (void)page.HandleTap(hit.x + hit.width * 0.5F, hit.y + hit.height * 0.5F);
          break;
        }
      }
      return true;
    };

    const bool focused = SweepFor(
      page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT, [&page, theirs] { return page.FocusedSystem() == theirs; }, false,
      closeAnySheet);
    Assert::IsTrue(focused, L"a rival's system could not be focused by any tap");
    Assert::IsTrue(page.OpenPanel() != Lockstep::MainPage::Panel::Place, L"a rival's system opened a build sheet");
  }

  TEST_METHOD(TheQueueNeverExceedsThePurse)
  {
    // Two rows and a purse that covers either but not both: whatever the sweep queues and takes
    // back, the running total stays inside the purse, which is the rule `Match::Validate` applies.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsTrue(state.orders.builds.size() >= 2, L"the opening board offers two buildings");
    state.player.credits = state.orders.builds[0].cost;
    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    const bool exceeded = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                   [&page] { return page.State().orders.QueuedBuildCost() > page.State().player.credits; });
    Assert::IsFalse(exceeded, L"the screen queued more than the purse covers");
  }
};

// The build sheet as a GRID OF TILES (ADR-107). The sheet stopped being a column of 44px rows and
// became four 282x96 tiles, which changes every rectangle on it -- so what these press is the tile,
// found in the hit list rather than at a coordinate, exactly as the sweeps above find a row.
//
// **Each one opens the sheet by pressing what opens it.** A sheet reached by setting `m_panel`
// would be a sheet nobody could have got to, and the composition that decides a tile's state runs
// in `DrawPanel` -- so a test that did not draw would be asserting about an empty vector.
TEST_CLASS(BuildTileTapTests)
{
public:
  TEST_METHOD(EveryTileQueuesTheBuildItDraws)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    Assert::IsTrue(OpenPlaceSheet(page, renderers), L"nothing on the screen opened a build sheet");

    // A system with nothing on it offers its two buildings at level one, which is the choice the
    // opening board is about (ADR-069). The bastion and the lane are designed and unbuilt.
    const std::vector<Lockstep::MainPage::HitRegion> tiles = TilesOn(page);
    Assert::AreEqual(std::size_t{2}, tiles.size(), L"a system with nothing built does not offer two tiles");
    Assert::IsTrue(page.State().orders.builds[static_cast<std::size_t>(tiles[0].index)].kind !=
                     page.State().orders.builds[static_cast<std::size_t>(tiles[1].index)].kind,
                   L"the two tiles are the same building");
    for (const Lockstep::MainPage::HitRegion& tile : tiles)
    {
      Assert::AreEqual(1U, page.State().orders.builds[static_cast<std::size_t>(tile.index)].level,
                       L"a tile on a system with nothing built does not buy level one");
    }

    // **Re-found between taps, not remembered.** The first tap puts the purse sentence above the
    // grid, which makes the sheet a line taller and moves every tile up: a second tap at a
    // remembered coordinate would land on the tile above the one it meant.
    for (std::size_t pass = 0; pass < 2; ++pass)
    {
      const std::vector<Lockstep::MainPage::HitRegion> offered = TilesOn(page);
      const auto next = std::find_if(offered.begin(), offered.end(),
                                     [&page](const Lockstep::MainPage::HitRegion& _tile)
                                     {
                                       const auto& queued = page.State().orders.queuedBuilds;
                                       return std::find(queued.begin(), queued.end(), _tile.index) == queued.end();
                                     });
      Assert::IsTrue(next != offered.end(), L"a tile that is not queued yet is no longer on the sheet");

      (void)page.HandleTap(next->x + next->width * 0.5F, next->y + next->height * 0.5F);
      renderers.Begin();
      DrawPage(page, renderers);
      Assert::AreEqual(pass + 1, page.State().orders.queuedBuilds.size(), L"tapping a tile did not queue its build");
    }

    Assert::AreEqual(std::size_t{2}, Lockstep::OrdersOf(page.State()).builds.size(), L"and both became orders");
  }

  TEST_METHOD(AQueuedTileTakesItselfBack)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    Assert::IsTrue(OpenPlaceSheet(page, renderers), L"nothing on the screen opened a build sheet");

    const std::vector<Lockstep::MainPage::HitRegion> tiles = TilesOn(page);
    Assert::IsFalse(tiles.empty(), L"the sheet offers no tile to press");
    const std::int32_t queuedRow = tiles.front().index;
    (void)page.HandleTap(tiles.front().x + tiles.front().width * 0.5F, tiles.front().y + tiles.front().height * 0.5F);
    renderers.Begin();
    DrawPage(page, renderers);
    Assert::AreEqual(std::size_t{1}, page.State().orders.queuedBuilds.size(), L"the tile did not queue anything");

    // The same tile, found again where the taller sheet left it. A queued tile says `TAP TO TAKE
    // BACK` on it and this is that tap (ADR-053).
    const std::vector<Lockstep::MainPage::HitRegion> after = TilesOn(page);
    const auto same = std::find_if(after.begin(), after.end(),
                                   [queuedRow](const Lockstep::MainPage::HitRegion& _tile) { return _tile.index == queuedRow; });
    Assert::IsTrue(same != after.end(), L"a queued tile stopped being a target, so it cannot be taken back");

    (void)page.HandleTap(same->x + same->width * 0.5F, same->y + same->height * 0.5F);
    renderers.Begin();
    DrawPage(page, renderers);
    Assert::IsTrue(page.State().orders.queuedBuilds.empty(), L"tapping a queued tile did not take it back");
  }

  TEST_METHOD(ATileBeyondThePurseRegistersNoHit)
  {
    // A purse that covers the cheaper building and not the dearer one, so the sheet has one tile
    // that is a target and one that is not (ADR-053, ADR-078). Both are still DRAWN -- what is
    // being asserted is that only one of them is a rectangle a finger can do anything with.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsTrue(state.orders.builds.size() >= 2, L"the opening board offers two buildings");

    std::uint32_t cheapest = state.orders.builds.front().cost;
    std::uint32_t dearest = cheapest;
    for (const Lockstep::BuildRow& row : state.orders.builds)
    {
      cheapest = std::min(cheapest, row.cost);
      dearest = std::max(dearest, row.cost);
    }
    Assert::IsTrue(cheapest < dearest, L"every building costs the same, so this test proves nothing");
    state.player.credits = cheapest;

    Lockstep::MainPage page;
    page.Create(std::move(state));
    Headless renderers;
    Assert::IsTrue(OpenPlaceSheet(page, renderers), L"nothing on the screen opened a build sheet");

    const std::vector<Lockstep::MainPage::HitRegion> tiles = TilesOn(page);
    Assert::AreEqual(std::size_t{1}, tiles.size(), L"a tile the purse cannot cover is still a target");
    Assert::AreEqual(cheapest, page.State().orders.builds[static_cast<std::size_t>(tiles.front().index)].cost,
                     L"the target is not the one the purse covers");
  }

  TEST_METHOD(ARisingSystemRegistersNoHitOnAnyTile)
  {
    // The system is building, so the lock refuses a second construction whatever its kind
    // (ADR-069). The sheet keeps the whole ladder visible -- the rising tile and the inert one
    // beside it, priced and marked with the tick it becomes orderable on -- and none of it is a
    // target (ADR-070, ADR-107).
    BuildingCapital built = CapitalThatIsBuilding();
    Assert::IsTrue(built.system != Lockstep::EventRefs::NONE, L"the capital is not on the viewer's own map");

    Lockstep::MainPage page;
    page.Create(std::move(built.state));

    Headless renderers;
    Assert::IsTrue(OpenPlaceSheet(page, renderers, built.system), L"the building capital opened no build sheet");
    Assert::IsTrue(TilesOn(page).empty(), L"a system that is already building offered a tile to press");

    // And it drew more than the one rising row, which is the whole point of keeping them.
    std::size_t onThisSystem = 0;
    for (const Lockstep::BuildRow& row : page.State().orders.builds)
    {
      if (row.system == page.State().graph.systems[static_cast<std::size_t>(built.system)].id)
      {
        ++onThisSystem;
      }
    }
    Assert::IsTrue(onThisSystem > 1, L"a system that is building has nothing on its sheet to plan against");
  }

  TEST_METHOD(ALaneTileIsDrawnAndPressableWhenTheFlagIsSet)
  {
    // `BuildRow::isTradeLane` is set by nothing (ADR-039's open question), so the lane tile is
    // styled here against the day it is -- and forced, so that day is not the first time anybody
    // finds out whether it draws. The flag is the whole difference: an amber icon, `PROPOSE n CR`
    // where a price goes, and the partner where the level ladder goes.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsFalse(state.orders.builds.empty());

    const std::int32_t laneSystem = state.orders.builds.front().system;
    std::uint32_t laneCost = 0;
    for (Lockstep::BuildRow& row : state.orders.builds)
    {
      if (row.system != laneSystem)
      {
        continue;
      }
      row.isTradeLane = true;
      row.building = "Trade lane";
      row.title = "Trade lane → Pell";
      row.detail = "+6 a tick each side · P2 must accept";
      row.partner = "P2";
      row.ticks = 4;
      laneCost = row.cost;
      break;
    }
    Assert::IsTrue(laneCost > 0, L"the forced lane row carries no price");

    Lockstep::MainPage page;
    page.Create(std::move(state));
    Headless renderers;
    Assert::IsTrue(OpenPlaceSheet(page, renderers, PositionOf(page.State(), laneSystem)), L"the lane's system opened no build sheet");

    const std::vector<Lockstep::MainPage::HitRegion> tiles = TilesOn(page);
    const auto lane = std::find_if(tiles.begin(), tiles.end(), [&page](const Lockstep::MainPage::HitRegion& _tile)
                                   { return page.State().orders.builds[static_cast<std::size_t>(_tile.index)].isTradeLane; });
    Assert::IsTrue(lane != tiles.end(), L"the lane tile registered no hit");

    const std::string proposed = std::format("PROPOSE {} CR", laneCost);
    const bool drawn = std::any_of(renderers.text.DrawnStrings().begin(), renderers.text.DrawnStrings().end(),
                                   [&proposed](const Neuron::FontRenderer::DrawnString& _drawn) { return _drawn.text == proposed; });
    Assert::IsTrue(drawn, L"the lane tile does not say what proposing it costs");

    // And it is the LAST slot: economy, war, defence, diplomacy, in that order on every sheet.
    Assert::AreEqual(lane->index, tiles.back().index, L"the lane is not in the grid's last slot");
  }
};

// Answering offers, which is the half of decision three the client could get wrong silently: the
// buttons drew and the taps landed, and with two offers open they would have answered the wrong
// one, or one of them would have had to wait a lock it could expire in (ADR-068).
// The place sheet: one system, what it can build and what is standing on it, in one sheet reached
// from every door (ADR-112). What is asserted here is the two rules that are new -- the sheet stays
// inside half the map pane, and the section carrying the move stays reachable when the rest of the
// body has to scroll to make that true.
TEST_CLASS(PlaceSheetTapTests)
{
public:
  /// The tallest body this sheet can produce: four buildings on one system, a fleet standing on it,
  /// and a build already queued so the purse sentence takes the help line (ADR-078).
  ///
  /// **Forced rather than played to**, for the reason the lane tile is: nothing composes a bastion
  /// or a trade lane yet (ADR-107's reserved slots), so the day one does must not be the first time
  /// anybody finds out what a full grid does to the sheet's height.
  [[nodiscard]] static Lockstep::MatchState AFullPlace(std::int32_t& _outSystem)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsFalse(state.orders.builds.empty(), L"a fresh match offers something to build");

    const Lockstep::BuildRow seed = state.orders.builds.front();
    _outSystem = PositionOf(state, seed.system);
    Assert::IsTrue(_outSystem != Lockstep::EventRefs::NONE, L"the build row names a system this viewer cannot see");

    // One row per role, so `TileSlotOf` spreads them over all four slots and the grid is two rows.
    state.orders.builds.clear();
    for (const std::uint8_t kind : {std::uint8_t{1}, std::uint8_t{0}, std::uint8_t{2}})
    {
      Lockstep::BuildRow row = seed;
      row.kind = kind;
      row.cost = 5;
      row.isTradeLane = false;
      state.orders.builds.push_back(row);
    }
    Lockstep::BuildRow lane = seed;
    lane.isTradeLane = true;
    lane.cost = 5;
    lane.partner = "P2";
    state.orders.builds.push_back(lane);

    // A queued build, so the sheet carries the purse sentence rather than nothing.
    state.player.credits = 100;
    state.orders.queuedBuilds.push_back(0);

    // And a fleet standing on it, which is the section that has to stay reachable.
    Assert::IsFalse(state.fleets.empty(), L"the opening board has no fleet to stand anywhere");
    for (Lockstep::Fleet& fleet : state.fleets)
    {
      if (fleet.owner == state.viewer)
      {
        fleet.from = _outSystem;
        fleet.to = _outSystem;
        fleet.order = Lockstep::FleetStance::Hold;
        fleet.underWay = false;
        break;
      }
    }
    return state;
  }

  TEST_METHOD(AFullPlaceSheetStaysInsideHalfTheMapPane)
  {
    // **The rule ADR-052 stated and nothing measured against** until a sheet held a grid and a
    // fleet list at once. The body is capped and scrolls rather than the sheet growing over the
    // systems the choice is about.
    std::int32_t system = Lockstep::EventRefs::NONE;
    Lockstep::MainPage page;
    page.Create(AFullPlace(system));

    Headless renderers;
    Assert::IsTrue(OpenPlaceSheet(page, renderers, system), L"the full place opened no sheet");
    Assert::IsTrue(SheetBoundsOf(page).height <= Lockstep::MainPage::SHEET_MAP_SHARE + 0.01F,
                   L"a full place sheet takes more than half the map pane");
  }

  TEST_METHOD(TheFleetsSectionStaysReachableWhileTheGridScrolls)
  {
    // **The move is what a pinned section is for** (ADR-093's shape, ADR-112's subject): the grid is
    // what overflows and the fleets are what a player came for, so the fleets are pinned above the
    // bar and the grid is what a notch moves.
    std::int32_t system = Lockstep::EventRefs::NONE;
    Lockstep::MainPage page;
    page.Create(AFullPlace(system));

    Headless renderers;
    Assert::IsTrue(OpenPlaceSheet(page, renderers, system), L"the full place opened no sheet");

    const std::vector<Lockstep::MainPage::HitRegion> tiles = TilesOn(page);
    Assert::IsTrue(!tiles.empty() && tiles.size() < 4, L"the capped body drew every tile, so nothing scrolled and this proves nothing");
    Assert::IsFalse(FleetControlsOn(page).empty(), L"the fleets section was scrolled away with the grid");

    // Notches over the sheet move the grid, and the pinned section does not move with them. More
    // than one, because a block is the unit and the first of them is the 26-pixel `BUILD` band: a
    // notch that only takes the band off the top leaves the same row of tiles under it, which is
    // correct and is why this counts rather than asserting on the first.
    bool different = false;
    for (std::int32_t notch = 0; notch < 4 && !different; ++notch)
    {
      (void)page.HandleZoom(-1, Lockstep::Frame::SCREEN_WIDTH * 0.5F, Lockstep::Frame::SCREEN_HEIGHT - 100.0F);
      renderers.Begin();
      DrawPage(page, renderers);
      Assert::IsFalse(FleetControlsOn(page).empty(), L"scrolling the grid took the fleets section with it");

      const std::vector<Lockstep::MainPage::HitRegion> after = TilesOn(page);
      different = !after.empty() && after.front().index != tiles.front().index;
    }
    Assert::IsTrue(different, L"no number of notches brought the second row of tiles onto the sheet");
  }

  TEST_METHOD(AQueuedMoveIsTakenBackFromTheSheetItWasGivenOn)
  {
    // The third place an order can be taken back, and the one the player is already looking at
    // (ADR-112). A fleet with a move queued wears the same committed state a queued build does.
    std::int32_t system = Lockstep::EventRefs::NONE;
    Lockstep::MainPage page;
    page.Create(AFullPlace(system));

    Headless renderers;
    Assert::IsTrue(OpenPlaceSheet(page, renderers, system), L"the full place opened no sheet");

    const std::vector<Lockstep::MainPage::HitRegion> controls = FleetControlsOn(page);
    Assert::IsFalse(controls.empty(), L"the sheet carries no fleet to move");
    const std::int32_t fleet = controls.front().index;
    const std::int32_t from = page.State().fleets[static_cast<std::size_t>(fleet)].from;

    // Take the move onto the map from the sheet, light the first destination it offers, and send it.
    (void)page.HandleTap(controls.front().x + controls.front().width * 0.5F, controls.front().y + controls.front().height * 0.5F);
    renderers.Begin();
    DrawPage(page, renderers);
    Assert::IsTrue(page.MoveOrder().has_value(), L"the sheet's MOVE did not take the move onto the map");

    for (const Lockstep::MainPage::HitRegion& hit : std::vector<Lockstep::MainPage::HitRegion>(page.Hits()))
    {
      if (hit.action == Lockstep::MainPage::Action::ChooseDestination)
      {
        (void)page.HandleTap(hit.x + hit.width * 0.5F, hit.y + hit.height * 0.5F);
        break;
      }
    }
    Assert::IsTrue(page.MoveOrder().has_value() && page.MoveOrder()->selected != Lockstep::EventRefs::NONE,
                   L"nothing on the map lit a destination");
    Assert::AreEqual(from, page.State().fleets[static_cast<std::size_t>(fleet)].to,
                     L"lighting a destination ordered the fleet, where it should take a SEND to do that");

    renderers.Begin();
    DrawPage(page, renderers);
    const auto send = std::ranges::find_if(page.Hits(), [](const Lockstep::MainPage::HitRegion& _hit)
                                           { return _hit.action == Lockstep::MainPage::Action::SendMove; });
    Assert::IsTrue(send != page.Hits().end(), L"a destination is lit and nothing on the screen sends it");
    (void)page.HandleTap(send->x + send->width * 0.5F, send->y + send->height * 0.5F);
    Assert::AreNotEqual(from, page.State().fleets[static_cast<std::size_t>(fleet)].to, L"SEND ordered the fleet nowhere");

    // Back to the place, where the fleet is still listed -- an ordered fleet has left the system it
    // is standing at and not the place it belongs to, which is the whole of `FleetsAtPlace`.
    Assert::IsTrue(OpenPlaceSheet(page, renderers, system), L"the place sheet did not reopen after the order");
    const std::vector<Lockstep::MainPage::HitRegion> committed = FleetControlsOn(page);
    const auto same =
      std::find_if(committed.begin(), committed.end(), [fleet](const Lockstep::MainPage::HitRegion& _hit) { return _hit.index == fleet; });
    Assert::IsTrue(same != committed.end(), L"a fleet with a move queued vanished from the place it was ordered off");
    Assert::IsTrue(same->action == Lockstep::MainPage::Action::CancelFleetOrder, L"and its control does not take the move back");

    (void)page.HandleTap(same->x + same->width * 0.5F, same->y + same->height * 0.5F);
    Assert::AreEqual(from, page.State().fleets[static_cast<std::size_t>(fleet)].to, L"the take-back left the fleet ordered away");
    Assert::IsTrue(Lockstep::OrdersOf(page.State()).fleetOrders.empty(), L"and the order is still in the set that goes to the server");
  }
};

// The price is on the button, and the purse it is priced against is the one the queue left
// (ADR-053, ADR-078).
TEST_CLASS(PurseSentenceTests)
{
public:
  // The other sentence that competes for a sheet's help slot: what a system that is already
  // building says about itself (ADR-070, ADR-107). Pure, so the counting is assertable without a
  // screen -- which is the whole reason it is not composed inline in `DrawPanel`.
  TEST_METHOD(ARisingSentenceCountsItsTicksInWords)
  {
    Assert::AreEqual(std::string{"Xerev cannot take another order until this lands. Two of three ticks are in."},
                     Lockstep::MainPage::RisingSentence("Xerev", 2, 3));

    // One tick in is singular, and nothing to count is not a sentence.
    Assert::AreEqual(std::string{"Pell cannot take another order until this lands. One of two ticks is in."},
                     Lockstep::MainPage::RisingSentence("Pell", 1, 2));
    Assert::AreEqual(std::string{"Pell cannot take another order until this lands. Zero of one ticks are in."},
                     Lockstep::MainPage::RisingSentence("Pell", 0, 1));
    Assert::IsTrue(Lockstep::MainPage::RisingSentence("Pell", 0, 0).empty(),
                   L"a system with no ticks to count still claimed to be building");

    // A build cannot be more ticks in than it is long, whatever arithmetic reaches it: a remembered
    // system carries no construction, so a tick number from the wrong tick is the shape of the bug
    // (ADR-022).
    Assert::AreEqual(Lockstep::MainPage::RisingSentence("Pell", 3, 3), Lockstep::MainPage::RisingSentence("Pell", 9, 3));
  }

  TEST_METHOD(NothingQueuedSaysNothing)
  {
    // The top bar already carries the purse. A sheet repeating it under every header would be a
    // line that is noise on every sheet but the one it is about.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    Assert::IsTrue(page.State().orders.queuedBuilds.empty(), L"this fixture starts with an empty queue");
    Assert::IsTrue(page.PurseSentence().empty(), L"a sheet with nothing queued explained an arithmetic nobody did");
  }

  TEST_METHOD(AQueuedBuildIsAccountedForAgainstThePurse)
  {
    // The complaint this answers, in its own numbers: a row reading `30 CR - NEED 4 MORE` under a
    // top bar reading `46 CR`. Both are right and the sheet said neither why.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsFalse(state.orders.builds.empty(), L"a fresh match offers something to build");

    state.player.credits = 46;
    state.orders.builds.front().cost = 20;
    state.orders.queuedBuilds.push_back(0);

    Lockstep::MainPage page;
    page.Create(std::move(state));

    const std::string sentence = page.PurseSentence();
    Assert::IsTrue(sentence.find("26") != std::string::npos, L"the sentence does not say what is left at the lock");
    Assert::IsTrue(sentence.find("20") != std::string::npos, L"nor what the queue has already taken");
    Assert::IsTrue(sentence.find("46") != std::string::npos, L"nor the purse on the top bar it has to be reconciled with");
  }

  TEST_METHOD(AQueueBeyondThePurseLeavesNothingRatherThanGoingUnder)
  {
    // `QueuedBuildCost` is unsigned and so is the purse. A queue bigger than the purse is a state
    // the guard refuses to reach, and the arithmetic that reports it must not wrap around instead.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.player.credits = 5;
    state.orders.builds.front().cost = 20;
    state.orders.queuedBuilds.push_back(0);

    Lockstep::MainPage page;
    page.Create(std::move(state));
    Assert::IsTrue(page.PurseSentence().find("0 credits") != std::string::npos,
                   L"an overdrawn purse reported something other than nothing");
  }
};

} // namespace LockstepTests
