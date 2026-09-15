// RailTapTests.cpp -- the locks rail, tapped: its rows as links and its one take-back cell (ADR-112),
// and the band that scrolls (ADR-101).

#include "pch.h"
#include "CppUnitTest.h"

#include "Headless.h"

#include "MainPage.h"
#include "SnapshotView.h"

#include "MatchSimulation.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

// The locks rail's rows are links to what they are about (ADR-060). They give no order -- the
// digest is still the order surface -- so what is asserted here is where a tap LANDS you.
TEST_CLASS(LocksRailTapTests)
{
public:
  /// A state with one build queued, so the rail has a BUILDS row to tap.
  [[nodiscard]] static Lockstep::MatchState WithAQueuedBuild(const Lockstep::MatchSimulation& _simulation)
  {
    Lockstep::MatchState state = ViewOfSeatZero(_simulation);
    Assert::IsFalse(state.orders.builds.empty(), L"a fresh match offers something to build");
    state.orders.queuedBuilds.push_back(0);
    return state;
  }

  TEST_METHOD(AQueuedBuildRowOpensTheSheetThatQueuedIt)
  {
    // The row says `SHIPYARD - PELL` / `QUEUED -20` and the only way to take it back was to find
    // Pell on the map again. Tapping the row is the shorter route to the same sheet.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = WithAQueuedBuild(*simulation);
    const std::int32_t system = state.orders.builds.front().system;

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, SCREEN_WIDTH - ORDERS_RAIL, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Place; });
    Assert::IsTrue(opened, L"no row on the locks rail opens the build sheet");

    const std::int32_t focused = page.FocusedSystem();
    Assert::IsTrue(focused >= 0 && focused < static_cast<std::int32_t>(page.State().graph.systems.size()));
    Assert::AreEqual(system, page.State().graph.systems[static_cast<std::size_t>(focused)].id,
                     L"the row opened a sheet about somebody else's system");
  }

  /// Everything the rail recorded, as `action` at `x`, so a failure names what it found rather than
  /// a coordinate.
  [[nodiscard]] static std::vector<Lockstep::MainPage::HitRegion> RailHits(const Lockstep::MainPage& _page)
  {
    std::vector<Lockstep::MainPage::HitRegion> found;
    for (const Lockstep::MainPage::HitRegion& hit : _page.Hits())
    {
      if (hit.x >= SCREEN_WIDTH - ORDERS_RAIL)
      {
        found.push_back(hit);
      }
    }
    return found;
  }

  TEST_METHOD(TheTakeBackCellUnqueuesTheOrderItsRowIsAbout)
  {
    // **The `×` is its own 44-pixel cell and the rest of the row is the link** (ADR-112). One row
    // shape for two kinds of order, so the cell has to name two different arrays -- a build row and
    // a fleet -- and it does that with two actions rather than one index that means both (ADR-057).
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(WithAQueuedBuild(*simulation));

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    const std::vector<Lockstep::MainPage::HitRegion> rail = RailHits(page);
    const auto cell = std::ranges::find_if(rail, [](const Lockstep::MainPage::HitRegion& _hit)
                                           { return _hit.action == Lockstep::MainPage::Action::ToggleBuild; });
    Assert::IsTrue(cell != rail.end(), L"a queued build has no take-back cell on the rail");
    Assert::AreEqual(44.0F, cell->width, 0.01F, L"the take-back cell is not the touch floor wide");
    Assert::AreEqual(44.0F, cell->height, 0.01F, L"nor the touch floor tall");

    // The row it sits on still opens the place, which is the half that must not be swallowed by it.
    const auto link =
      std::ranges::find_if(rail, [cell](const Lockstep::MainPage::HitRegion& _hit)
                           { return _hit.action == Lockstep::MainPage::Action::OpenSystem && std::abs(_hit.y - cell->y) < 0.01F; });
    Assert::IsTrue(link != rail.end(), L"the row carrying the take-back cell is not a link to its place");
    Assert::IsTrue(link->x + link->width <= cell->x + 0.01F, L"the link overlaps the take-back cell, so one of them cannot be hit");

    (void)page.HandleTap(cell->x + cell->width * 0.5F, cell->y + cell->height * 0.5F);
    Assert::IsTrue(page.State().orders.queuedBuilds.empty(), L"the take-back cell did not unqueue the build");
  }

  TEST_METHOD(AnUnorderedFleetIsARowOfItsOwn)
  {
    // **The one thing this column never said** (ADR-112): a fleet with nothing to do is invisible on
    // every other surface of this screen, so a player reading a list of what goes in at the lock had
    // no way to see what does not.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    Assert::IsTrue(page.State().orders.queuedBuilds.empty(), L"the opening board has something queued, so this fixture is wrong");

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    const bool named = std::ranges::any_of(renderers.text.DrawnStrings(), [](const Neuron::FontRenderer::DrawnString& _drawn)
                                           { return _drawn.text.find("NO MOVE") != std::string::npos; });
    Assert::IsTrue(named, L"a standing fleet with no order drew no row saying so");

    // And it is a target, because the point of the row is that it is the one thing on this column a
    // player can still do something about.
    const std::vector<Lockstep::MainPage::HitRegion> rail = RailHits(page);
    const bool orderable = std::ranges::any_of(rail, [](const Lockstep::MainPage::HitRegion& _hit)
                                               { return _hit.action == Lockstep::MainPage::Action::BeginMove; });
    Assert::IsTrue(orderable, L"the unordered fleet's row leads nowhere");
  }

  TEST_METHOD(APlacesRowOpensThePlaceItNames)
  {
    // `PLACES` replaces `FLEETS` and `BUILDS` (ADR-112): one row per system you hold, and the row is
    // the shortest route to everything that system can do this tick.
    const auto simulation = PlayedMatch(4);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    const bool banded = std::ranges::any_of(renderers.text.DrawnStrings(),
                                            [](const Neuron::FontRenderer::DrawnString& _drawn) { return _drawn.text == "PLACES"; });
    Assert::IsTrue(banded, L"the rail has no PLACES section");

    // Every row in it names a system the viewer holds, and opens that system.
    std::size_t opened = 0;
    for (const Lockstep::MainPage::HitRegion& hit : RailHits(page))
    {
      if (hit.action != Lockstep::MainPage::Action::OpenSystem || hit.index < 0)
      {
        continue;
      }
      (void)page.HandleTap(hit.x + hit.width * 0.5F, hit.y + hit.height * 0.5F);
      Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::Place, L"a rail row opened no place sheet");
      Assert::AreEqual(page.State().viewer, page.State().graph.systems[static_cast<std::size_t>(hit.index)].owner,
                       L"a rail row named a system the viewer does not hold");
      ++opened;
      renderers.Begin();
      DrawPage(page, renderers);
      break;
    }
    Assert::IsTrue(opened > 0, L"no row on the rail opened a place");
  }

  TEST_METHOD(ALockedRailRowFocusesAndOpensNothing)
  {
    // Screen 06: at the lock every control on this screen is inert, and a row that opened a sheet
    // would be a sheet offering orders for a tick that is already resolving. Focusing is not an
    // order, so it survives.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = WithAQueuedBuild(*simulation);
    state.orders.locked = true;

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, SCREEN_WIDTH - ORDERS_RAIL, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() != Lockstep::MainPage::Panel::None; });
    Assert::IsFalse(opened, L"a locked rail row opened a sheet");
    Assert::IsTrue(page.FocusedSystem() != Lockstep::EventRefs::NONE, L"and it focused nothing either, so the tap did nothing at all");
  }
};

// The locks rail scrolls, because 44px rows made it have to (ADR-101).
TEST_CLASS(RailScrollTests)
{
public:
  /// A board whose rail cannot possibly fit: ten fleets across several systems, plus the other
  /// three sections under them.
  [[nodiscard]] static Lockstep::MatchState ACrowdedRail(const Lockstep::MatchSimulation& _simulation)
  {
    Lockstep::MatchState state = ViewOfSeatZero(_simulation);
    Assert::IsFalse(state.fleets.empty());
    Assert::IsTrue(state.graph.systems.size() >= 5U);

    const Lockstep::Fleet original = state.fleets.front();
    state.fleets.clear();
    for (std::int32_t index = 0; index < 10; ++index)
    {
      Lockstep::Fleet fleet = original;
      fleet.id = 100 + index;
      fleet.name = std::format("FLT {}", index + 1);
      fleet.owner = state.viewer;
      fleet.ships = static_cast<std::uint32_t>(index + 1);
      fleet.order = Lockstep::FleetStance::Hold;
      fleet.underWay = false;
      // Spread across five systems, so the section carries bands as well as rows.
      fleet.from = index % 5;
      fleet.to = index % 5;
      state.fleets.push_back(std::move(fleet));
    }
    return state;
  }

  TEST_METHOD(AWheelOverTheRailScrollsItAndStopsAtBothEnds)
  {
    const auto simulation = PlayedMatch(4);
    Lockstep::MainPage page;
    page.Create(ACrowdedRail(*simulation));

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    // A notch away from the player moves down the column, as it does on the digest.
    Assert::IsTrue(page.HandleZoom(-1, 1150.0F, 300.0F), L"a notch over the rail scrolled nothing");

    renderers.Begin();
    DrawPage(page, renderers);

    // Spun hard against the bottom it reports no change, so an idle frame costs no redraw.
    for (std::int32_t again = 0; again < 60; ++again)
    {
      (void)page.HandleZoom(-1, 1150.0F, 300.0F);
      renderers.Begin();
      DrawPage(page, renderers);
    }
    Assert::IsFalse(page.HandleZoom(-1, 1150.0F, 300.0F), L"the rail has no bottom");

    for (std::int32_t back = 0; back < 80; ++back)
    {
      (void)page.HandleZoom(1, 1150.0F, 300.0F);
      renderers.Begin();
      DrawPage(page, renderers);
    }
    Assert::IsFalse(page.HandleZoom(1, 1150.0F, 300.0F), L"the rail has no top");
  }

  TEST_METHOD(ARailThatFitsDoesNotScrollAtAll)
  {
    // The opening board: one fleet, nothing queued. There is nothing below the fold, so the gesture
    // must do nothing rather than move a column that is already whole.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    Assert::IsFalse(page.HandleZoom(-1, 1150.0F, 300.0F), L"a rail with room to spare scrolled");
  }

  TEST_METHOD(AScrolledRowIsNeitherDrawnNorTappable)
  {
    // `ShapeRenderer` has no clip, so a row scrolled past the top is culled rather than clipped --
    // and a culled row must take its hit rectangle with it, or there is an invisible control sitting
    // over the help line.
    const auto simulation = PlayedMatch(4);
    Lockstep::MainPage page;
    page.Create(ACrowdedRail(*simulation));

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    const auto railHits = [&page]
    {
      std::size_t count = 0;
      for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
      {
        // The rail's own column, below the header.
        count += hit.x >= SCREEN_WIDTH - ORDERS_RAIL && hit.y > TOP_BAR ? 1U : 0U;
      }
      return count;
    };
    const std::size_t before = railHits();
    Assert::IsTrue(before > 0, L"the crowded rail offered no targets at all");

    // Every hit still inside the band, wherever it is scrolled to.
    for (std::int32_t step = 0; step < 6; ++step)
    {
      (void)page.HandleZoom(-1, 1150.0F, 300.0F);
      renderers.Begin();
      DrawPage(page, renderers);

      for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
      {
        const bool onTheRail = hit.x >= SCREEN_WIDTH - ORDERS_RAIL && hit.y > TOP_BAR;
        if (onTheRail)
        {
          Assert::IsTrue(hit.y >= TOP_BAR, L"a rail hit escaped above the band");

          // The footer, not the screen. `ALL LOCK TOGETHER` is pinned in the last 30 pixels and
          // nothing that scrolls may reach it -- which is what the page band's own targets stop
          // exactly at.
          Assert::IsTrue(hit.y + hit.height <= SCREEN_HEIGHT - 30.0F, L"a rail hit escaped below the band");
        }
      }
    }
  }

  /// The page band's two halves, by the action only they carry.
  [[nodiscard]] static std::vector<Lockstep::MainPage::HitRegion> PageBand(const Lockstep::MainPage& _page)
  {
    std::vector<Lockstep::MainPage::HitRegion> band;
    for (const Lockstep::MainPage::HitRegion& hit : _page.Hits())
    {
      if (hit.action == Lockstep::MainPage::Action::PageRail)
      {
        band.push_back(hit);
      }
    }
    return band;
  }

  TEST_METHOD(TheBandIsWhatAFingerScrollsTheRailWith)
  {
    // **The wheel is the shortcut and the band is the control** (ADR-098, ADR-101). A rail that can
    // only be scrolled by a mouse gesture is a rail whose bottom a touch player never reaches, so
    // what this asserts is that the tap works, not that the wheel does.
    const auto simulation = PlayedMatch(4);
    Lockstep::MainPage page;
    page.Create(ACrowdedRail(*simulation));

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    // At the top there is one half only: a control offering to go up from the top is a control that
    // does nothing, and the digest band has never drawn one either (ADR-080).
    std::vector<Lockstep::MainPage::HitRegion> band = PageBand(page);
    Assert::AreEqual(std::size_t{1}, band.size(), L"the band at the top of a crowded rail is not one half");
    Assert::AreEqual(1, band.front().index, L"the only half at the top is not the one that goes down");
    Assert::IsTrue(band.front().height >= 44.0F, L"the band is under the touch floor");

    Assert::IsTrue(page.HandleTap(band.front().x + 10.0F, band.front().y + 10.0F), L"the band's down half did nothing");

    renderers.Begin();
    DrawPage(page, renderers);

    // And now both, because there is something in both directions.
    band = PageBand(page);
    Assert::AreEqual(std::size_t{2}, band.size(), L"a scrolled rail does not offer both directions");

    const auto up = std::ranges::find_if(band, [](const Lockstep::MainPage::HitRegion& _hit) { return _hit.index == -1; });
    Assert::IsTrue(up != band.end(), L"a scrolled rail offers no way back up");
    Assert::IsTrue(page.HandleTap(up->x + 10.0F, up->y + 10.0F), L"the band's up half did nothing");

    renderers.Begin();
    DrawPage(page, renderers);

    // Back where it started, which is the half of a pager that is easy to get wrong.
    Assert::AreEqual(std::size_t{1}, PageBand(page).size(), L"the rail did not come back to its top");
  }

  /// Every string the last frame was asked to draw, which is where a band's copy still is a
  /// sentence rather than glyph boxes.
  [[nodiscard]] static bool WasDrawn(const Headless& _renderers, std::string_view _text)
  {
    return std::ranges::any_of(_renderers.text.DrawnStrings(),
                               [_text](const Neuron::FontRenderer::DrawnString& _drawn) { return _drawn.text == _text; });
  }

  TEST_METHOD(TheBandSaysWhatIsBelowItAndThenSaysEnd)
  {
    // **The wording is the decision** (ADR-101), so it is read out of a real frame rather than
    // eyeballed: `N MORE - SIGNALS >` tells a player whether the thing they are looking for is down
    // there, which `>` on its own never did.
    const auto simulation = PlayedMatch(4);
    Lockstep::MainPage page;
    page.Create(ACrowdedRail(*simulation));

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);
    renderers.Begin();
    DrawPage(page, renderers);

    // At the top: no way up, and what is below named by its section.
    Assert::IsFalse(WasDrawn(renderers, "‹ UP"), L"the band offered a way up from the top");

    const auto below = std::ranges::find_if(renderers.text.DrawnStrings(), [](const Neuron::FontRenderer::DrawnString& _drawn)
                                            { return _drawn.text.find(" MORE") != std::string::npos && _drawn.text.ends_with("›"); });
    Assert::IsTrue(below != renderers.text.DrawnStrings().end(), L"the band did not say what is below it");
    Assert::IsTrue(below->text.find("PLACES") != std::string::npos || below->text.find("SIGNALS") != std::string::npos ||
                     below->text.find("PROPOSALS") != std::string::npos,
                   L"the band did not name the section below the fold");

    // Spun to the bottom: a way up, and `END` rather than a count of nothing.
    for (std::int32_t again = 0; again < 60; ++again)
    {
      (void)page.HandleZoom(-1, 1150.0F, 300.0F);
      renderers.Begin();
      DrawPage(page, renderers);
    }

    Assert::IsTrue(WasDrawn(renderers, "‹ UP"), L"a rail at its bottom offered no way back up");
    Assert::IsTrue(WasDrawn(renderers, "END"), L"a rail at its bottom did not say END");
  }

  TEST_METHOD(ARailThatFitsHasNoBandAndKeepsTheRoom)
  {
    // The band costs 44 pixels of column, so a rail that fits must not be paying for it.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    for (std::int32_t frame = 0; frame < 3; ++frame)
    {
      renderers.Begin();
      DrawPage(page, renderers);
    }

    Assert::AreEqual(std::size_t{0}, PageBand(page).size(), L"a rail with room to spare drew a page band");
    Assert::IsFalse(WasDrawn(renderers, "END"), L"a rail with room to spare drew the band's copy");
  }
};

} // namespace LockstepTests
