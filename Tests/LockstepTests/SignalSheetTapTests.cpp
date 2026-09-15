// SignalSheetTapTests.cpp -- the signal picker, tapped (ADR-039, ADR-064, ADR-093).

#include "pch.h"
#include "CppUnitTest.h"

#include "Headless.h"

#include "MainPage.h"
#include "SnapshotView.h"

#include <algorithm>
#include <format>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

TEST_CLASS(SignalPickerTapTests)
{
public:
  /// Finds the tap that opens the signal picker, and reports where it was.
  [[nodiscard]] static bool OpenThePicker(Lockstep::MainPage& _page, Headless& _renderers, std::int32_t& _outX, std::int32_t& _outY)
  {
    for (std::int32_t y = TOP_BAR; y < SCREEN_HEIGHT; y += STEP)
    {
      for (std::int32_t x = SCREEN_WIDTH - ORDERS_RAIL; x < SCREEN_WIDTH; x += STEP)
      {
        _renderers.Begin();
        DrawPage(_page, _renderers);
        (void)_page.HandleTap(static_cast<float>(x), static_cast<float>(y));
        if (_page.OpenPanel() == Lockstep::MainPage::Panel::SignalList)
        {
          _outX = x;
          _outY = y;
          return true;
        }
      }
    }
    return false;
  }

  TEST_METHOD(TheSignalsRailOpensThePicker)
  {
    // ADR-039: the SIGNALS header is the only section header on that rail that is a control, and it
    // is the only way in, because there is no map object to hang "concede" on.
    const auto simulation = PlayedMatch(14);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    std::int32_t x = 0;
    std::int32_t y = 0;
    Assert::IsTrue(OpenThePicker(page, renderers, x, y), L"nothing on the orders rail opens the signal list");
  }

  TEST_METHOD(ASignalInThePickerCanBeQueued)
  {
    const auto simulation = PlayedMatch(14);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    std::int32_t openX = 0;
    std::int32_t openY = 0;
    Assert::IsTrue(OpenThePicker(page, renderers, openX, openY));

    // Bottom-up, and reopened whenever a stray tap on the map puts another panel over it.
    const auto reopen = [&page, &renderers, openX, openY]
    {
      if (page.OpenPanel() == Lockstep::MainPage::Panel::SignalList)
      {
        return false;
      }
      renderers.Begin();
      DrawPage(page, renderers);
      (void)page.HandleTap(static_cast<float>(openX), static_cast<float>(openY));
      return true;
    };

    const bool queued = SweepFor(
      page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT, [&page] { return !page.State().orders.queuedSignals.empty(); },
      true, reopen);
    Assert::IsTrue(queued, L"no row in the signal picker can be queued");

    // And it becomes an order, which is the half a screenshot could never show.
    const Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
    const bool somethingWentOut =
      !orders.proposals.empty() || !orders.withdrawals.empty() || !orders.cancellations.empty() || orders.concede;
    Assert::IsTrue(somethingWentOut, L"a queued signal produced no order");
  }

  TEST_METHOD(ConcedingTakesTwoTapsOnTheSameRow)
  {
    // **The riskiest interaction on the screen**, and the reason it is worth pressing rather than
    // photographing: conceding cannot be undone once it resolves, so the first tap must arm and
    // only the second must queue. A screenshot can show the row saying TAP AGAIN TO CONFIRM and
    // cannot show whether the first tap already sent it.
    //
    // The concede row's position is FOUND rather than assumed: swept for, and anything else that
    // queues on the way is tapped again to take it back. A test that knew where the row was would
    // stop testing the screen and start testing a coordinate.
    const auto simulation = PlayedMatch(14);

    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    std::int32_t openX = 0;
    std::int32_t openY = 0;
    Assert::IsTrue(OpenThePicker(page, renderers, openX, openY));

    std::int32_t concede = -1;
    for (std::size_t index = 0; index < page.State().orders.signals.size(); ++index)
    {
      if (page.State().orders.signals[index].kind == Lockstep::SignalKind::Concede)
      {
        concede = static_cast<std::int32_t>(index);
      }
    }
    Assert::IsTrue(concede >= 0, L"the picker does not offer a concede at all");

    const auto queuedIs = [&page](std::int32_t _row)
    {
      const std::vector<std::int32_t>& queued = page.State().orders.queuedSignals;
      return std::ranges::find(queued, _row) != queued.end();
    };

    // One tap, redrawing first only if the last one landed on something. Same bargain `SweepFor`
    // makes, spelled out here because this loop presses the same place several times.
    bool stale = true;
    const auto tap = [&page, &renderers, &stale](std::int32_t _x, std::int32_t _y)
    {
      if (stale)
      {
        renderers.Begin();
        DrawPage(page, renderers);
      }
      stale = page.HandleTap(static_cast<float>(_x), static_cast<float>(_y));
    };

    std::int32_t rowX = 0;
    std::int32_t rowY = 0;
    for (std::int32_t y = SCREEN_HEIGHT - STEP; y > TOP_BAR && rowY == 0; y -= STEP)
    {
      for (std::int32_t x = 0; x < SCREEN_WIDTH; x += STEP)
      {
        // Reopened before each pair, because a stray tap on the map puts the BUILD panel over the
        // picker and there is nothing in the sweep's path that would put it back.
        //
        // FOUND AGAIN RATHER THAN REMEMBERED. The SIGNALS header sits under BUILDS on the rail, so
        // a build queued by a stray tap earlier in this sweep pushes it down a row and the opening
        // coordinate goes stale (`Design/UI/README.md`, "Photographing the build", says the same of
        // a capture script). A remembered coordinate made this test pass only while the rail
        // happened not to grow.
        if (page.OpenPanel() != Lockstep::MainPage::Panel::SignalList)
        {
          tap(openX, openY);
        }

        // The cheap tap is the remembered coordinate; this is what happens when it goes stale. The
        // SIGNALS header sits under BUILDS on the rail, so a build queued earlier in this sweep
        // pushes it down a row (`Design/UI/README.md`, "Photographing the build", says the same of
        // a capture script). Searching again is slow, so it runs only when the cheap tap missed.
        if (page.OpenPanel() != Lockstep::MainPage::Panel::SignalList)
        {
          std::int32_t againX = 0;
          std::int32_t againY = 0;
          if (OpenThePicker(page, renderers, againX, againY))
          {
            openX = againX;
            openY = againY;
          }
          stale = true;
        }

        tap(x, y);
        tap(x, y);

        if (queuedIs(concede))
        {
          rowX = x;
          rowY = y;
          break;
        }

        // Something else took the taps. Put it back, so the sweep does not accumulate a rail full
        // of orders nobody asked for. Bounded, because a loop that keeps tapping until a condition
        // holds is a hang waiting for the day the condition stops being reachable.
        for (std::int32_t attempt = 0; attempt < 4 && !page.State().orders.queuedSignals.empty(); ++attempt)
        {
          tap(x, y);
        }
      }
    }
    Assert::AreNotEqual(0, rowY, L"the concede row cannot be reached by tapping");

    // Now the same spot, from a clean page: one tap must not be enough.
    Lockstep::MainPage again;
    again.Create(ViewOfSeatZero(*simulation));
    Assert::IsTrue(OpenThePicker(again, renderers, openX, openY));

    renderers.Begin();
    DrawPage(again, renderers);
    (void)again.HandleTap(static_cast<float>(rowX), static_cast<float>(rowY));
    Assert::IsTrue(again.State().orders.queuedSignals.empty(), L"one tap conceded the match");
    Assert::IsFalse(Lockstep::OrdersOf(again.State()).concede, L"one tap produced a concede order");

    renderers.Begin();
    DrawPage(again, renderers);
    (void)again.HandleTap(static_cast<float>(rowX), static_cast<float>(rowY));
    Assert::IsFalse(again.State().orders.queuedSignals.empty(), L"a second tap on the armed row did nothing");
    Assert::IsTrue(Lockstep::OrdersOf(again.State()).concede, L"the confirmed concede produced no concede order");

    // And a third takes it back, because it has not resolved yet and is an edit like any other.
    renderers.Begin();
    DrawPage(again, renderers);
    (void)again.HandleTap(static_cast<float>(rowX), static_cast<float>(rowY));
    Assert::IsTrue(again.State().orders.queuedSignals.empty(), L"a queued concede could not be taken back");
  }

  TEST_METHOD(AFullSheetKeepsItsSixSignalsAndTheConcede)
  {
    // **The concede is pinned below the cap and costs nothing** (ADR-093). It used to sit inside the
    // six, so a full sheet bought it by dropping a real signal -- and its band by dropping another.
    // Now the sheet shows its six and the concede is under them, above `CANCEL`.
    const auto simulation = PlayedMatch(14);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    // Five offers and a concede: six rows, which is exactly the cap, so the band cannot also fit.
    state.orders.signals.clear();
    state.orders.queuedSignals.clear();
    for (std::int32_t index = 0; index < 5; ++index)
    {
      state.orders.signals.push_back(Lockstep::SignalRow{
        .kind = Lockstep::SignalKind::ShareScouting, .title = std::format("Share scouting - P{}", index + 2), .to = index + 1});
    }
    state.orders.signals.push_back(Lockstep::SignalRow{.kind = Lockstep::SignalKind::Concede, .title = "Concede"});
    state.orders.availableSignals = 6;

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    std::int32_t openX = 0;
    std::int32_t openY = 0;
    Assert::IsTrue(OpenThePicker(page, renderers, openX, openY));

    // Two taps on the same place, bottom-up. The concede is the only row that needs a pair; the
    // five above it queue and unqueue within one, so the sweep leaves nothing behind it.
    bool stale = true;
    const auto tap = [&page, &renderers, &stale](std::int32_t _x, std::int32_t _y)
    {
      if (stale)
      {
        renderers.Begin();
        DrawPage(page, renderers);
      }
      stale = page.HandleTap(static_cast<float>(_x), static_cast<float>(_y));
    };

    bool queued = false;
    for (std::int32_t y = SCREEN_HEIGHT - STEP; y > TOP_BAR && !queued; y -= STEP)
    {
      for (std::int32_t x = 0; x < SCREEN_WIDTH && !queued; x += STEP)
      {
        if (page.OpenPanel() != Lockstep::MainPage::Panel::SignalList)
        {
          tap(openX, openY);
        }
        tap(x, y);
        tap(x, y);

        const std::vector<std::int32_t>& sent = page.State().orders.queuedSignals;
        queued = std::ranges::find(sent, 5) != sent.end();
      }
    }
    Assert::IsTrue(queued, L"the concede row was pushed off the sheet by the signals above it");

    // **And the signals it used to displace are still on the sheet.** That is what pinning bought:
    // the concede no longer costs a row. Index 4 is the fifth signal, which under the old rule shared
    // the six with the concede and its band. A fresh page, because the sweep above taps every row
    // twice and a non-concede signal queues and unqueues within one pair.
    Lockstep::MainPage full;
    full.Create(ViewOfSeatZero(*simulation));
    std::int32_t fullX = 0;
    std::int32_t fullY = 0;
    Assert::IsTrue(OpenThePicker(full, renderers, fullX, fullY));

    const bool fifth = SweepFor(
      full, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
      [&full]
      {
        const std::vector<std::int32_t>& queuedNow = full.State().orders.queuedSignals;
        return std::ranges::find(queuedNow, 4) != queuedNow.end();
      },
      true,
      [&full, &renderers, fullX, fullY]
      {
        if (full.OpenPanel() == Lockstep::MainPage::Panel::SignalList)
        {
          return false;
        }
        renderers.Begin();
        DrawPage(full, renderers);
        return full.HandleTap(static_cast<float>(fullX), static_cast<float>(fullY));
      });
    Assert::IsTrue(fifth, L"a full sheet lost a signal to the concede below it");
  }

  TEST_METHOD(AnOverfullSheetStillOffersTheConcede)
  {
    // The test above is about a sheet that is EXACTLY full. This is about one that overflows: nine
    // signals and a concede, so the six-row cap clips three of them into `+N MORE` and the concede
    // is not among the six at all. It is drawn below them regardless (ADR-093), which is the whole
    // point of pinning it -- the one control that must always be reachable, on the board busy enough
    // to want it.
    const auto simulation = PlayedMatch(14);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    state.orders.signals.clear();
    state.orders.queuedSignals.clear();
    for (std::int32_t index = 0; index < 9; ++index)
    {
      state.orders.signals.push_back(Lockstep::SignalRow{
        .kind = Lockstep::SignalKind::ShareScouting, .title = std::format("Share scouting - P{}", index + 2), .to = index + 1});
    }
    const std::int32_t concede = static_cast<std::int32_t>(state.orders.signals.size());
    state.orders.signals.push_back(Lockstep::SignalRow{.kind = Lockstep::SignalKind::Concede, .title = "Concede"});
    state.orders.availableSignals = static_cast<std::uint32_t>(state.orders.signals.size());

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    std::int32_t openX = 0;
    std::int32_t openY = 0;
    Assert::IsTrue(OpenThePicker(page, renderers, openX, openY));

    bool stale = true;
    const auto tap = [&page, &renderers, &stale](std::int32_t _x, std::int32_t _y)
    {
      if (stale)
      {
        renderers.Begin();
        DrawPage(page, renderers);
      }
      stale = page.HandleTap(static_cast<float>(_x), static_cast<float>(_y));
    };

    bool queued = false;
    for (std::int32_t y = SCREEN_HEIGHT - STEP; y > TOP_BAR && !queued; y -= STEP)
    {
      for (std::int32_t x = 0; x < SCREEN_WIDTH && !queued; x += STEP)
      {
        if (page.OpenPanel() != Lockstep::MainPage::Panel::SignalList)
        {
          tap(openX, openY);
        }
        tap(x, y);
        tap(x, y);

        const std::vector<std::int32_t>& sent = page.State().orders.queuedSignals;
        queued = std::ranges::find(sent, concede) != sent.end();
      }
    }
    Assert::IsTrue(queued, L"ten offers pushed the concede off the sheet, so the match cannot be conceded");
  }

  TEST_METHOD(ALockedRailQueuesNothing)
  {
    // Screen 06. At the lock every control on this screen is inert, and "inert" has to mean the tap
    // does nothing rather than the button merely looking grey.
    const auto simulation = PlayedMatch(14);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.orders.locked = true;

    Lockstep::MainPage page;
    page.Create(state);

    Headless renderers;
    const bool queued = SweepFor(page, renderers, DrawPage, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, [&page]
                                 { return !page.State().orders.queuedSignals.empty() || !page.State().orders.queuedBuilds.empty(); });
    Assert::IsFalse(queued, L"a locked screen accepted an order");
  }
};

} // namespace LockstepTests
