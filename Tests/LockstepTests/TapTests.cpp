// TapTests.cpp -- the buttons, pressed by something other than a finger.
//
// **Three ADRs in a row shipped controls that had never been pressed.** ADR-037's BOT toggles,
// ADR-038's five connection-dialog buttons, ADR-039's signal picker: wired, compiled, photographed
// even -- and never once activated, because the only thing that could press them was a synthetic
// tap on an unlocked desktop, and the desktop was locked. "It draws correctly" and "it does the
// right thing when you hit it" are different claims and only one of them was being made.
//
// The obstacle was never the tap. It was that every page builds its hit list WHILE IT DRAWS --
// `AddHit` sits beside the `FillRect` that put the button there, which is exactly what stops the
// two drifting apart -- and drawing needed a GPU. It does not any more (ADR-041, and RENDER-01
// stage 4): a renderer records into an ordinary vector whether or not it was ever given a device,
// so these drive the append path that ships rather than a sibling of it.
//
// THESE TESTS DO NOT KNOW WHERE ANYTHING IS. They sweep a region and look for the effect, the way
// `Build/TapRehearsal.ps1` scans the screen for a button rather than being told a coordinate. That
// is deliberate: a test carrying a hardcoded pixel is a test that has to be edited every time the
// layout moves, and it would be asserting the layout rather than the behaviour. What is being
// claimed here is "somewhere on this screen there is a control that does this", which is the claim
// that was missing.

#include "pch.h"
#include "CppUnitTest.h"

#include "ConnectionDialog.h"
#include "MainPage.h"
#include "SeatsPage.h"
#include "SnapshotView.h"

#include "BotPolicy.h"
#include "MatchSimulation.h"
#include "TickResolver.h"

#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

namespace
{

constexpr std::int32_t SCREEN_WIDTH = 1280;
constexpr std::int32_t SCREEN_HEIGHT = 720;

/// How far apart the sweep's taps are. Eight pixels is the glyph box and smaller than any control
/// on any of these screens, so nothing can hide between two of them.
///
/// Whole pixels, and the sweeps below count in them rather than in floats. A tap IS a pixel -- the
/// pointer reports integers -- and a float counter accumulating a step is a loop whose last
/// iteration is somewhere nobody chose.
constexpr std::int32_t STEP = 8;

/// The two bands a sweep is sometimes confined to: the top bar, which is not what any of these
/// tests are about, and the orders rail down the right-hand side.
constexpr std::int32_t TOP_BAR = 44;
constexpr std::int32_t ORDERS_RAIL = 260;

/// Two renderers with no device behind them. One per sweep rather than one per tap: `BeginFrame`
/// resets the slice, so a single pair can draw as many frames as a test needs.
struct Headless
{
  Neuron::ShapeRenderer shapes;
  Neuron::FontRenderer text;

  void Begin()
  {
    shapes.BeginFrame(0);
    text.BeginFrame(0);
  }
};

/// Sweeps a rectangle, and stops when `_done` says so.
///
/// **It redraws only after a tap that hit something**, which is the difference between a sweep that
/// takes a second and one that takes thirty. The hit list belongs to the frame it was built in, so
/// a stale list would be pressing buttons where they used to be -- but a tap that matched no hit
/// changed nothing, and `HandleTap` returning false is exactly that statement. Every page in this
/// tree already reports it, because the composition root needs to know whether a tap was consumed
/// before deciding whether to send an order.
///
/// `_fromBottom` sweeps upward. A panel carries its close button at the top and its rows below it,
/// so a downward sweep shuts the panel before it ever reaches a row -- which is a true thing about
/// the screen and a useless way to find out whether a row works.
///
/// `_ensure` puts the screen back before a tap when a previous one wandered off it -- on the main
/// page a stray tap on the map opens the BUILD panel over whatever was being tested. It does its
/// own drawing and says whether it acted, because acting invalidates the frame.
template <typename Page, typename Draw>
[[nodiscard]] bool SweepFor(Page& _page, Headless& _renderers, Draw _draw, std::int32_t _left, std::int32_t _top, std::int32_t _right,
                            std::int32_t _bottom, const std::function<bool()>& _done, bool _fromBottom = false,
                            const std::function<bool()>& _ensure = {})
{
  bool stale = true;
  for (std::int32_t row = _top; row < _bottom; row += STEP)
  {
    const std::int32_t y = _fromBottom ? _bottom - (row - _top) - STEP : row;
    for (std::int32_t x = _left; x < _right; x += STEP)
    {
      if (_ensure && _ensure())
      {
        stale = true;
      }

      if (stale)
      {
        _renderers.Begin();
        _draw(_page, _renderers);
      }

      stale = _page.HandleTap(static_cast<float>(x), static_cast<float>(y));
      if (_done())
      {
        return true;
      }
    }
  }
  return false;
}

void DrawDialog(Lockstep::ConnectionDialog& _dialog, Headless& _renderers)
{
  _renderers.Begin();
  _dialog.Draw(_renderers.shapes, _renderers.text);
}

/// A dialog showing one kind, with the facts it needs.
[[nodiscard]] Lockstep::ConnectionDialog::Action PressSomething(Lockstep::ConnectionDialog::Kind _kind,
                                                                const Lockstep::ConnectionDialog::Facts& _facts,
                                                                Lockstep::ConnectionDialog::Action _wanted)
{
  Headless renderers;
  Lockstep::ConnectionDialog dialog;
  dialog.Update(_kind, _facts, 0.0);

  // Drawn once. Nothing a tap does to this dialog changes what it draws -- an action is reported
  // to the caller and the card is not laid out again -- so the hit list cannot go stale under it.
  DrawDialog(dialog, renderers);

  Lockstep::ConnectionDialog::Action found = Lockstep::ConnectionDialog::Action::None;
  for (std::int32_t y = 0; y < SCREEN_HEIGHT && found != _wanted; y += STEP)
  {
    for (std::int32_t x = 0; x < SCREEN_WIDTH && found != _wanted; x += STEP)
    {
      (void)dialog.HandleTap(static_cast<float>(x), static_cast<float>(y));
      const Lockstep::ConnectionDialog::Action action = dialog.TakeAction();
      if (action != Lockstep::ConnectionDialog::Action::None)
      {
        found = action;
      }
    }
  }
  return found;
}

[[nodiscard]] std::unique_ptr<Lockstep::MatchSimulation> PlayedMatch(std::int32_t _ticks)
{
  Lockstep::MatchRules rules;
  rules.playerCount = 6;

  std::vector<std::optional<Lockstep::BotPolicy>> bots(6, std::optional<Lockstep::BotPolicy>{Lockstep::BotPolicy::ExpandNear});
  bots[0].reset();

  auto simulation = std::make_unique<Lockstep::MatchSimulation>(rules, 0x5441'5053'2121'2121ULL, bots);
  for (std::int32_t tick = 0; tick < _ticks; ++tick)
  {
    simulation->Resolve();
  }
  return simulation;
}

[[nodiscard]] Lockstep::MatchState ViewOfSeatZero(const Lockstep::MatchSimulation& _simulation)
{
  const Lockstep::PlayerId seat{0};
  return Lockstep::ViewOf(Lockstep::Snapshot::For(_simulation.State(), seat), Lockstep::Snapshot::DigestFor(_simulation.LastTick(), seat),
                          600);
}

/// Seat zero with two offers in front of it, one from each of two rivals.
///
/// `HoldForTicks` because it is the offer with no board precondition (SignalTests says the same of
/// its own helper); what these tests are about is the buttons, not how the offer got there.
[[nodiscard]] Lockstep::MatchState SeatZeroWithTwoOffers()
{
  Lockstep::MatchRules rules;
  rules.playerCount = 6;
  Lockstep::Match match = Lockstep::Match::Create(rules, 0x5349'474E'414C'5321ULL);

  std::vector<Lockstep::OrderSet> sets;
  for (const std::int32_t sender : {1, 2})
  {
    Lockstep::OrderSet set;
    set.player = Lockstep::PlayerId{sender};
    set.proposals.push_back(Lockstep::ProposalOrder{.to = Lockstep::PlayerId{0}, .kind = Lockstep::ProposalKind::HoldForTicks, .ticks = 2});
    sets.push_back(std::move(set));
  }

  Lockstep::TickLog log;
  match = Lockstep::TickResolver::Resolve(match, {.orders = sets}, log);

  const Lockstep::PlayerId seat{0};
  return Lockstep::ViewOf(Lockstep::Snapshot::For(match, seat), Lockstep::Snapshot::DigestFor(log, seat), 600);
}

void DrawPage(Lockstep::MainPage& _page, Headless& _renderers)
{
  _page.DrawWorld(_renderers.shapes, _renderers.text);
  _page.DrawInterface(_renderers.shapes, _renderers.text);
}

void DrawSeats(Lockstep::SeatsPage& _page, Headless& _renderers)
{
  _page.DrawWorld(_renderers.shapes, _renderers.text);
  _page.DrawInterface(_renderers.shapes, _renderers.text);
}

} // namespace

TEST_CLASS(ConnectionDialogTapTests)
{
public:
  TEST_METHOD(ARefusedTokenOffersAWayBackToTheField)
  {
    // ADR-038's EDIT TOKEN, which is the whole point of the refusal dialog: the commonest refusal
    // is a typo, and the screen behind this one has the field with the typo in it.
    Lockstep::ConnectionDialog::Facts facts;
    facts.reason = Neuron::RefusalReason::UnknownToken;
    facts.canGoBack = true;

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::EditToken ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Refused, facts, Lockstep::ConnectionDialog::Action::EditToken),
                   L"EDIT TOKEN cannot be pressed");
  }

  TEST_METHOD(ASeatInUseOffersARetry)
  {
    // The refusal that stops being true on its own: the seat frees when the other link drops.
    Lockstep::ConnectionDialog::Facts facts;
    facts.reason = Neuron::RefusalReason::AlreadyConnected;
    facts.canGoBack = true;

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Retry ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Refused, facts, Lockstep::ConnectionDialog::Action::Retry),
                   L"RETRY cannot be pressed");
  }

  TEST_METHOD(ALostConnectionOffersARetryAndAQuit)
  {
    Lockstep::ConnectionDialog::Facts facts;
    facts.lockCountdown = "00:14:09";

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Retry ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Lost, facts, Lockstep::ConnectionDialog::Action::Retry),
                   L"RETRY NOW cannot be pressed");
    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Quit ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Lost, facts, Lockstep::ConnectionDialog::Action::Quit),
                   L"QUIT cannot be pressed");
  }

  TEST_METHOD(AFinishedMatchOffersTheLastDigest)
  {
    Lockstep::ConnectionDialog::Facts facts;
    facts.standings = "1 OF 6 - SCORE 85";

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::ViewLastDigest ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Finished, facts, Lockstep::ConnectionDialog::Action::ViewLastDigest),
                   L"VIEW LAST DIGEST cannot be pressed");
  }

  TEST_METHOD(AConnectingDialogCanBeCanceled)
  {
    Lockstep::ConnectionDialog::Facts facts;
    facts.server = "127.0.0.1:7341";

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Cancel ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Connecting, facts, Lockstep::ConnectionDialog::Action::Cancel),
                   L"CANCEL cannot be pressed");
  }

  TEST_METHOD(WithNothingBehindItThereIsNoWayBackOnlyOut)
  {
    // `canGoBack` false is the match loop, where the join screen is a whole match ago. A BACK there
    // would be a button that returns to a screen this process left twenty minutes ago.
    Lockstep::ConnectionDialog::Facts facts;
    facts.reason = Neuron::RefusalReason::UnknownToken;
    facts.canGoBack = false;

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Back !=
                     PressSomething(Lockstep::ConnectionDialog::Kind::Refused, facts, Lockstep::ConnectionDialog::Action::Back),
                   L"BACK was offered with nothing behind it");
    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Quit ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Refused, facts, Lockstep::ConnectionDialog::Action::Quit),
                   L"QUIT cannot be pressed");
  }

  TEST_METHOD(TheDialogSwallowsEveryTapItIsOver)
  {
    // A tap reaching the board behind a CONNECTION LOST dialog would be an order edit the client
    // cannot send, and the player would have no way to tell which of their taps counted.
    Headless renderers;
    Lockstep::ConnectionDialog dialog;
    dialog.Update(Lockstep::ConnectionDialog::Kind::Lost, Lockstep::ConnectionDialog::Facts{}, 0.0);
    DrawDialog(dialog, renderers);

    // The four corners, which are as far from the card as this screen goes.
    Assert::IsTrue(dialog.HandleTap(1.0F, 1.0F), L"a tap in the corner fell through the scrim");
    Assert::IsTrue(dialog.HandleTap(static_cast<float>(SCREEN_WIDTH) - 1.0F, static_cast<float>(SCREEN_HEIGHT) - 1.0F));
    Assert::IsTrue(dialog.TakeAction() == Lockstep::ConnectionDialog::Action::None, L"the scrim pressed a button");
  }

  TEST_METHOD(AHiddenDialogSwallowsNothing)
  {
    // The other half: when there is nothing wrong the dialog must be completely out of the way.
    Headless renderers;
    Lockstep::ConnectionDialog dialog;
    dialog.Update(Lockstep::ConnectionDialog::Kind::None, Lockstep::ConnectionDialog::Facts{}, 0.0);
    DrawDialog(dialog, renderers);

    Assert::IsFalse(dialog.Visible());
    Assert::IsFalse(dialog.HandleTap(640.0F, 360.0F), L"an invisible dialog swallowed a tap");
  }
};

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

  TEST_METHOD(AFullSheetDropsTheBandRatherThanTheConcede)
  {
    // The `CONCEDE` band counts against the six-row cap (ADR-064), so on a sheet that is already
    // full it is dropped rather than pushing the row it labels into `+N MORE`. A label must never
    // cost a control its place.
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
    Assert::IsTrue(queued, L"the concede row was pushed off the sheet by its own band");
  }

  TEST_METHOD(AnOverfullSheetStillOffersTheConcede)
  {
    // The band test above is about a sheet that is EXACTLY full. This is about one that overflows:
    // the concede is composed last and the sheet draws the first six, so a player with six offers
    // on the table had no concede row at all -- the one control that must always be reachable,
    // missing exactly when the board is busy enough to want it. It keeps the last visible slot
    // (ADR-064, extended 2026-09-13).
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

TEST_CLASS(SeatsPageTapTests)
{
public:
  TEST_METHOD(ASeatCanBeHandedToABot)
  {
    // ADR-037's BOT toggle, drawn and refused until that ADR and never pressed since.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};

    Headless renderers;
    const bool filled =
      SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
               [&page]
               {
                 const std::vector<std::optional<Lockstep::BotPolicy>> roster = page.Roster();
                 return std::ranges::any_of(roster, [](const std::optional<Lockstep::BotPolicy>& _seat) { return _seat.has_value(); });
               });
    Assert::IsTrue(filled, L"no seat on the seats screen can be given to a bot");
  }

  TEST_METHOD(TheHostsOwnSeatCannotBeGivenAway)
  {
    // The host is the process that owns the match. A bot in their seat would leave it with nothing
    // to draw. Seat one is the host's by default, and the whole sweep must not manage to flip it.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};

    Headless renderers;
    (void)SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, [] { return false; });

    // Every control on the screen, pressed, in every order the sweep happens to find. The host's
    // own seat has to come out of that still theirs.
    const std::vector<std::optional<Lockstep::BotPolicy>> roster = page.Roster();
    Assert::IsTrue(page.HostSeat() >= 0 && page.HostSeat() < static_cast<std::int32_t>(roster.size()));
    Assert::IsFalse(roster[static_cast<std::size_t>(page.HostSeat())].has_value(), L"the host gave their own seat to a bot");
  }

  TEST_METHOD(TheTokenRowStillWorksWhereTakeSeatUsedTo)
  {
    // `NEW TOKEN` shared a row with `TAKE SEAT`. Removing one of a pair of half-width buttons is
    // exactly the edit that leaves the other one drawn and unhittable, and nothing would say so.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};

    Headless renderers;

    // Any seat's token, not seat one's: the sweep selects a card before it reaches the panel, and
    // NEW TOKEN reissues whichever seat is selected.
    const std::vector<std::string> before = page.PlayingTokens();
    const bool reissued =
      SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, [&page, &before] { return page.PlayingTokens() != before; });
    Assert::IsTrue(reissued, L"no control on the seats screen reissues a token");
  }

  TEST_METHOD(AWaitingSeatCanBeMarkedWithoutBecomingABot)
  {
    // ADR-066's middle segment. `BOT AT T1` is the answer between "wait for them" and "play it
    // yourself", and the claim it has to make is that the seat is READY while still a person's --
    // which is what makes ENTER MATCH reachable when a friend does not show, without handing their
    // empire to a machine before they have had the chance to arrive.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};
    page.SetConnected(std::vector<bool>{true, false, false, false, false, false});

    Headless renderers;
    const bool marked = SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page]
                                 {
                                   for (std::int32_t seat = 0; seat < Lockstep::SeatsPage::SEAT_COUNT; ++seat)
                                   {
                                     if (page.BotTakesOverSeat(seat) && !page.Roster()[static_cast<std::size_t>(seat)].has_value())
                                     {
                                       return true;
                                     }
                                   }
                                   return false;
                                 });
    Assert::IsTrue(marked, L"no control marks a waiting seat for a bot without making it one now");
  }

  TEST_METHOD(EveryoneIsHereOnceTheSeatsAreBots)
  {
    // The rule that makes ENTER MATCH reachable when a friend does not show: a bot seat is never
    // waiting for anybody.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};
    page.SetConnected(std::vector<bool>{true, false, false, false, false, false});
    Assert::IsFalse(page.EveryoneIsHere(), L"five empty human seats counted as ready");

    Headless renderers;
    (void)SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, [&page] { return page.EveryoneIsHere(); });
    Assert::IsTrue(page.EveryoneIsHere(), L"a screen full of bots is still waiting for somebody");
  }
};

// The digest is taller than the column it is drawn in, and nothing scrolls (ADR-052 option C). An
// actor card collapses and the stack pages (ADR-061), and both are controls that have to be pressed.
TEST_CLASS(DigestOverflowTapTests)
{
public:
  /// A digest event about `_actor`, added to a real state so the page has a galaxy under it.
  static void AddEvent(Lockstep::MatchState& _state, Lockstep::OwnerId _actor, std::string _title)
  {
    Lockstep::DigestEvent event;
    event.kind = Lockstep::EventKind::Economy;
    event.actor = _actor;
    event.title = std::move(_title);
    event.detail = "Something happened, and here is the line that says so.";
    _state.digest.push_back(std::move(event));
  }

  /// Sweeps the digest column and reports where the tap that satisfied `_done` was.
  [[nodiscard]] static bool SweepDigest(Lockstep::MainPage& _page, Headless& _renderers, const std::function<bool()>& _done,
                                        std::int32_t& _outX, std::int32_t& _outY)
  {
    bool stale = true;
    for (std::int32_t y = TOP_BAR; y < SCREEN_HEIGHT; y += STEP)
    {
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(Lockstep::MainPage::DIGEST_WIDTH); x += STEP)
      {
        if (stale)
        {
          _renderers.Begin();
          DrawPage(_page, _renderers);
        }
        stale = _page.HandleTap(static_cast<float>(x), static_cast<float>(y));
        if (_done())
        {
          _outX = x;
          _outY = y;
          return true;
        }
      }
    }
    return false;
  }

  TEST_METHOD(AnActorCardOpensAndClosesFromItsTitle)
  {
    // An actor card is the only card whose body is a LIST, and the only one that can be dropped
    // without losing a fact: the title still names the rival and the stamp still counts them.
    const auto simulation = PlayedMatch(2);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.digest.clear();
    AddEvent(state, 1, "Halvorsen took Pell");
    AddEvent(state, 1, "Halvorsen proposes a lane");

    Lockstep::MainPage page;
    page.Create(std::move(state));
    Assert::IsTrue(page.ExpandedActor() == Lockstep::NOBODY, L"a digest opened with a card already open");

    Headless renderers;
    std::int32_t x = 0;
    std::int32_t y = 0;
    Assert::IsTrue(
      SweepDigest(page, renderers, [&page] { return page.ExpandedActor() == 1; }, x, y), L"nothing in the digest opens an actor card");

    renderers.Begin();
    DrawPage(page, renderers);
    (void)page.HandleTap(static_cast<float>(x), static_cast<float>(y));
    Assert::IsTrue(page.ExpandedActor() == Lockstep::NOBODY, L"the same title did not close the card again");
  }

  TEST_METHOD(OnlyOneActorCardIsOpenAtATime)
  {
    const auto simulation = PlayedMatch(2);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.digest.clear();
    AddEvent(state, 1, "Halvorsen took Pell");
    AddEvent(state, 1, "Halvorsen proposes a lane");
    AddEvent(state, 2, "Sorne took Dothan");
    AddEvent(state, 2, "Sorne proposes a lane");

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    std::int32_t x = 0;
    std::int32_t y = 0;
    Assert::IsTrue(SweepDigest(page, renderers, [&page] { return page.ExpandedActor() != Lockstep::NOBODY; }, x, y));
    const Lockstep::OwnerId first = page.ExpandedActor();

    // Another card OPEN, not merely the first one shut: the sweep passes over the first title again
    // on its way down and closing it is not what is being claimed here.
    Assert::IsTrue(
      SweepDigest(
        page, renderers, [&page, first] { return page.ExpandedActor() != first && page.ExpandedActor() != Lockstep::NOBODY; }, x, y),
      L"the second actor card could not be opened");
    Assert::IsTrue(page.ExpandedActor() != first, L"two actor cards were open at once");
  }

  TEST_METHOD(ADigestTallerThanTheColumnPages)
  {
    // Twenty cards is more than the column holds however they are laid out, which is the condition
    // the band exists for. What it must never do is drop one: page one has to be reachable again.
    const auto simulation = PlayedMatch(2);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.digest.clear();
    for (std::int32_t index = 0; index < 20; ++index)
    {
      AddEvent(state, Lockstep::NOBODY, std::format("Production +{}", index + 1));
    }

    Lockstep::MainPage page;
    page.Create(std::move(state));
    Assert::AreEqual(std::size_t{0}, page.DigestPage());

    Headless renderers;
    std::int32_t x = 0;
    std::int32_t y = 0;
    Assert::IsTrue(SweepDigest(
                     page, renderers, [&page] { return page.DigestPage() > 0; }, x, y),
                   L"a digest taller than the column offers no way to the rest of it");

    Assert::IsTrue(SweepDigest(page, renderers, [&page] { return page.DigestPage() == 0; }, x, y), L"there is no way back to page one");
  }
};

// A sheet is where somebody is in the middle of deciding something, and neither the lock nor the
// state that follows it takes that away (ADR-065).
TEST_CLASS(OpenSheetTapTests)
{
public:
  TEST_METHOD(ASheetSurvivesTheLockAndStillTakesNoOrder)
  {
    // An opening board carries `MOVE FLT n` on its one card (ADR-056), which is the only way to a
    // destination picker when no fleet is under way.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Destination; });
    Assert::IsTrue(opened, L"no control opens a destination picker on an opening board");

    // The lock arrives under it.
    page.Update(1.0e6);
    Assert::IsTrue(page.State().orders.locked);
    Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::Destination, L"the lock closed the sheet");

    std::vector<std::int32_t> before;
    for (const Lockstep::Fleet& fleet : page.State().fleets)
    {
      before.push_back(fleet.to);
    }

    const bool queued = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT, [&page]
                                 { return !page.State().orders.queuedBuilds.empty() || !page.State().orders.queuedSignals.empty(); });
    Assert::IsFalse(queued, L"a sheet left open at the lock took an order");
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
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::BuildList; });
    Assert::IsTrue(opened, L"nothing opens a build sheet on a played board");

    const std::int32_t about = page.FocusedSystem();
    Assert::IsTrue(about >= 0);
    const std::int32_t identity = page.State().graph.systems[static_cast<std::size_t>(about)].id;

    page.Create(page.State());
    Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::BuildList, L"a new state closed a sheet that still had a subject");

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
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::BuildList; });
    Assert::IsTrue(opened, L"no row on the locks rail opens the build sheet");

    const std::int32_t focused = page.FocusedSystem();
    Assert::IsTrue(focused >= 0 && focused < static_cast<std::int32_t>(page.State().graph.systems.size()));
    Assert::AreEqual(system, page.State().graph.systems[static_cast<std::size_t>(focused)].id,
                     L"the row opened a sheet about somebody else's system");
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

    Headless renderers;
    const bool queued = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return !page.State().orders.queuedBuilds.empty(); });
    Assert::IsTrue(queued, L"no control on the screen queues a build");
    Assert::AreEqual(std::size_t{1}, Lockstep::OrdersOf(page.State()).builds.size(), L"and it became an order");
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

    const bool offersTheRisingSystem =
      std::any_of(state.orders.builds.begin(), state.orders.builds.end(),
                  [capital](const Lockstep::BuildRow& _row) { return _row.system == capital.Index() && !_row.rising; });
    Assert::IsFalse(offersTheRisingSystem, L"a system already building was offered a second order to queue");

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
    Headless renderers;
    const bool focused = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                  [&page, theirs] { return page.FocusedSystem() == theirs; });
    Assert::IsTrue(focused, L"a rival's system could not be focused by any tap");
    Assert::IsTrue(page.OpenPanel() != Lockstep::MainPage::Panel::BuildList, L"a rival's system opened a build sheet");
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

// Answering offers, which is the half of decision three the client could get wrong silently: the
// buttons drew and the taps landed, and with two offers open they would have answered the wrong
// one, or one of them would have had to wait a lock it could expire in (ADR-068).
TEST_CLASS(ProposalAnswerTapTests)
{
public:
  TEST_METHOD(BothOffersCanBeAnsweredBeforeOneLock)
  {
    Lockstep::MainPage page;
    page.Create(SeatZeroWithTwoOffers());
    Assert::AreEqual(std::size_t{2}, page.State().proposals.size(), L"this test needs two offers on the table");

    // Sweep the digest column until both offers carry an answer. Every ACCEPT and DECLINE on the
    // screen is pressed on the way, which is the point: the answers must end up on different
    // offers rather than overwriting one another.
    Headless renderers;
    const bool answered = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH - ORDERS_RAIL, SCREEN_HEIGHT,
                                   [&page] { return page.State().orders.answers.size() == 2U; });
    Assert::IsTrue(answered, L"two offers on screen and no pair of taps answers both");

    const Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
    Assert::AreEqual(std::size_t{2}, orders.answers.size(), L"both answers did not reach one order set");
    Assert::IsTrue(orders.answers[0].proposal != orders.answers[1].proposal, L"both answers named the same offer");
  }

  TEST_METHOD(AnsweringOneOfferTwiceSendsOneAnswer)
  {
    Lockstep::MainPage page;
    page.Create(SeatZeroWithTwoOffers());

    Headless renderers;
    const bool first = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH - ORDERS_RAIL, SCREEN_HEIGHT,
                                [&page] { return !page.State().orders.answers.empty(); });
    Assert::IsTrue(first, L"nothing on the screen answers an offer");

    const std::int32_t offer = page.State().orders.answers.front().proposal;
    const bool accepted = page.State().orders.answers.front().accepted;

    // Press both buttons on that same offer as many times as they are found. However many taps it
    // takes, one offer is one answer -- the last one given.
    std::size_t answersForThatOffer = 0;
    (void)SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH - ORDERS_RAIL, SCREEN_HEIGHT,
                   [&page, offer, &answersForThatOffer]
                   {
                     answersForThatOffer = static_cast<std::size_t>(
                       std::count_if(page.State().orders.answers.begin(), page.State().orders.answers.end(),
                                     [offer](const Lockstep::ProposalAnswer& _answer) { return _answer.proposal == offer; }));
                     return answersForThatOffer > 1U;
                   });

    Assert::AreEqual(std::size_t{1}, answersForThatOffer, L"one offer collected more than one answer");

    const Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
    const std::size_t forThatOffer = static_cast<std::size_t>(
      std::count_if(orders.answers.begin(), orders.answers.end(), [&page, offer](const Lockstep::AnswerOrder& _answer)
                    { return _answer.proposal == Lockstep::ProposalId{page.State().proposals[static_cast<std::size_t>(offer)].id}; }));
    Assert::AreEqual(std::size_t{1}, forThatOffer, L"one offer produced more than one answer order");

    // And the answer that survived is a real one either way round.
    const bool stillThere = std::any_of(page.State().orders.answers.begin(), page.State().orders.answers.end(),
                                        [offer](const Lockstep::ProposalAnswer& _answer) { return _answer.proposal == offer; });
    Assert::IsTrue(stillThere, L"the answer vanished rather than being replaced");
    (void)accepted;
  }
};

} // namespace LockstepTests
