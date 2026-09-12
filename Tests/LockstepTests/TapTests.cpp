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
// two drifting apart -- and drawing needed a GPU. `CreateHeadless` (ADR-041) removes that: the same
// append path, into ordinary memory.
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

  Headless()
  {
    shapes.CreateHeadless();
    text.CreateHeadless();
  }

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
        if (page.OpenPanel() != Lockstep::MainPage::Panel::SignalList)
        {
          tap(openX, openY);
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

} // namespace LockstepTests
