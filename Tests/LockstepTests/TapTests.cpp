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
#include "DigestView.h"
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

/// Three renderers with no device behind them. One set per sweep rather than one per tap:
/// `BeginFrame` resets the slice, so a single set can draw as many frames as a test needs.
struct Headless
{
  Neuron::ShapeRenderer shapes;
  Neuron::FontRenderer text;
  Neuron::MeshRenderer meshes;

  void Begin()
  {
    shapes.BeginFrame();
    text.BeginFrame();
    meshes.BeginFrame();
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
  _page.DrawWorld(_renderers.shapes, _renderers.text, _renderers.meshes);
  _page.DrawInterface(_renderers.shapes, _renderers.text);
}

void DrawSeats(Lockstep::SeatsPage& _page, Headless& _renderers)
{
  _page.DrawWorld(_renderers.shapes, _renderers.text);
  _page.DrawInterface(_renderers.shapes, _renderers.text);
}

/// Where system `_id` sits in the view's own fogged list, which is the index every `OpenSystem`
/// carries (ADR-057). `EventRefs::NONE` when the viewer cannot see it.
[[nodiscard]] std::int32_t PositionOf(const Lockstep::MatchState& _state, std::int32_t _id)
{
  for (std::size_t index = 0; index < _state.graph.systems.size(); ++index)
  {
    if (_state.graph.systems[index].id == _id)
    {
      return static_cast<std::int32_t>(index);
    }
  }
  return Lockstep::EventRefs::NONE;
}

/// Opens a build sheet by PRESSING what opens one, and leaves the page drawn so its hit list is the
/// sheet's (ADR-058, ADR-107). `_system` is a position, or `NONE` for whichever opens first.
///
/// It presses rather than setting a field for the reason every test in this file does: what is
/// under test is the sheet the screen builds, and a sheet reached by any other route is a different
/// sheet. A tap that opens nothing focuses instead, which is why every candidate is tried.
[[nodiscard]] bool OpenBuildSheet(Lockstep::MainPage& _page, Headless& _renderers, std::int32_t _system = Lockstep::EventRefs::NONE)
{
  _renderers.Begin();
  DrawPage(_page, _renderers);

  // A COPY, because the next draw rebuilds the list this is being walked.
  const std::vector<Lockstep::MainPage::HitRegion> candidates = _page.Hits();
  for (const Lockstep::MainPage::HitRegion& hit : candidates)
  {
    if (hit.action != Lockstep::MainPage::Action::OpenSystem || hit.index < 0)
    {
      continue;
    }
    if (_system != Lockstep::EventRefs::NONE && hit.index != _system)
    {
      continue;
    }

    (void)_page.HandleTap(hit.x + hit.width * 0.5F, hit.y + hit.height * 0.5F);
    _renderers.Begin();
    DrawPage(_page, _renderers);
    if (_page.OpenPanel() == Lockstep::MainPage::Panel::BuildList)
    {
      return true;
    }
  }
  return false;
}

/// Every tile on the open sheet that is a target, as the rectangles `AddHit` recorded.
///
/// **`ToggleBuild` is not enough to identify one.** The digest's priced build button queues the same
/// order from the other side of the screen (ADR-053), so it carries the same action -- which is
/// correct and is why the guard behind them is one. What tells them apart is the column: the digest
/// is the leftmost 400 pixels and a sheet is drawn over the map pane, never into a rail.
[[nodiscard]] std::vector<Lockstep::MainPage::HitRegion> TilesOn(const Lockstep::MainPage& _page)
{
  std::vector<Lockstep::MainPage::HitRegion> tiles;
  for (const Lockstep::MainPage::HitRegion& hit : _page.Hits())
  {
    if (hit.action == Lockstep::MainPage::Action::ToggleBuild && hit.x >= Lockstep::Frame::DIGEST_WIDTH)
    {
      tiles.push_back(hit);
    }
  }
  return tiles;
}

/// A match in which seat zero's capital is already building something, and the tick log that says
/// so. Driven through a real order for the reason `ASystemAlreadyBuildingOffersNothingToQueue` is:
/// every index into `orders.builds` is composed by `ViewOf`, so a list edited afterwards is a list
/// of stale indices rather than of a rule (ADR-057).
struct BuildingCapital
{
  Lockstep::MatchState state;
  std::int32_t system = Lockstep::EventRefs::NONE;
};

[[nodiscard]] BuildingCapital CapitalThatIsBuilding()
{
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

  const Lockstep::PlayerId seat{0};
  BuildingCapital built{.state = Lockstep::ViewOf(Lockstep::Snapshot::For(match, seat), Lockstep::Snapshot::DigestFor(log, seat), 600)};
  built.system = PositionOf(built.state, capital.Index());
  return built;
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
    facts.standings = {Lockstep::ConnectionDialog::Facts::Standing{.text = "1ST  YOU        85", .isYou = true}};

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

  TEST_METHOD(AModalSwallowsEveryTapItIsOver)
  {
    // A tap reaching the board behind a REFUSED dialog would be an order edit against a connection
    // that is not there, and the player would have no way to tell which of their taps counted.
    Headless renderers;
    Lockstep::ConnectionDialog dialog;
    dialog.Update(Lockstep::ConnectionDialog::Kind::Refused, Lockstep::ConnectionDialog::Facts{}, 0.0);
    DrawDialog(dialog, renderers);

    Assert::IsTrue(dialog.Modal());

    // The four corners, which are as far from the card as this screen goes.
    Assert::IsTrue(dialog.HandleTap(1.0F, 1.0F), L"a tap in the corner fell through the scrim");
    Assert::IsTrue(dialog.HandleTap(static_cast<float>(SCREEN_WIDTH) - 1.0F, static_cast<float>(SCREEN_HEIGHT) - 1.0F));
    Assert::IsTrue(dialog.TakeAction() == Lockstep::ConnectionDialog::Action::None, L"the scrim pressed a button");
  }

  TEST_METHOD(ALostLinkIsABannerAndNotAModal)
  {
    // **The dialog told the player nothing they tapped would be sent, and then stopped them doing
    // the things that do not need sending** (ADR-085): reading the digest that arrived before the
    // drop, looking at the map, opening a sheet. A banner says the same sentence in 44 pixels.
    Headless renderers;
    Lockstep::ConnectionDialog dialog;
    dialog.Update(Lockstep::ConnectionDialog::Kind::Lost, Lockstep::ConnectionDialog::Facts{}, 0.0);
    DrawDialog(dialog, renderers);

    Assert::IsTrue(dialog.Visible(), L"a dropped link draws nothing at all");
    Assert::IsFalse(dialog.Modal(), L"a dropped link is still a modal");

    // The board below the band is reachable: the four corners of the map pane fall through.
    Assert::IsFalse(dialog.HandleTap(700.0F, 400.0F), L"the banner swallowed a tap on the map");
    Assert::IsFalse(dialog.HandleTap(1.0F, static_cast<float>(SCREEN_HEIGHT) - 1.0F), L"the banner swallowed a tap on the digest");
    Assert::IsTrue(dialog.TakeAction() == Lockstep::ConnectionDialog::Action::None);
  }

  TEST_METHOD(TheBannerStillOffersRetryAndQuit)
  {
    Assert::IsTrue(PressSomething(Lockstep::ConnectionDialog::Kind::Lost, Lockstep::ConnectionDialog::Facts{},
                                  Lockstep::ConnectionDialog::Action::Retry) == Lockstep::ConnectionDialog::Action::Retry,
                   L"the banner offers no way to retry now");
    Assert::IsTrue(PressSomething(Lockstep::ConnectionDialog::Kind::Lost, Lockstep::ConnectionDialog::Facts{},
                                  Lockstep::ConnectionDialog::Action::Quit) == Lockstep::ConnectionDialog::Action::Quit,
                   L"the banner offers no way out");
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

  /// Twenty cards is more than the column holds however they are laid out, which is the condition
  /// the band exists for.
  [[nodiscard]] static Lockstep::MatchState ATallDigest()
  {
    const auto simulation = PlayedMatch(2);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.digest.clear();
    for (std::int32_t index = 0; index < 20; ++index)
    {
      AddEvent(state, Lockstep::NOBODY, std::format("Production +{}", index + 1));
    }
    return state;
  }

  TEST_METHOD(ADigestTallerThanTheColumnPages)
  {
    // What the band must never do is drop a card: the top of the stack has to be reachable again.
    Lockstep::MainPage page;
    page.Create(ATallDigest());
    Assert::AreEqual(std::size_t{0}, page.DigestTop());

    Headless renderers;
    std::int32_t x = 0;
    std::int32_t y = 0;
    Assert::IsTrue(SweepDigest(
                     page, renderers, [&page] { return page.DigestTop() > 0; }, x, y),
                   L"a digest taller than the column offers no way to the rest of it");

    Assert::IsTrue(SweepDigest(page, renderers, [&page] { return page.DigestTop() == 0; }, x, y), L"there is no way back to the top");
  }

  TEST_METHOD(AWheelOverTheDigestScrollsItAndOverTheMapDoesNot)
  {
    // The wheel has been banked by `PointerInput` since ADR-009 and read by nothing (ADR-080). What
    // decides it is the pane under the pointer, so the same notch over two panes is two answers.
    Lockstep::MainPage page;
    page.Create(ATallDigest());

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    Assert::IsTrue(page.HandleZoom(-1, 100.0F, 300.0F), L"a notch over the digest scrolled nothing");
    Assert::AreEqual(std::size_t{1}, page.DigestTop(), L"and it moved by something other than one card");

    // Over the map the same notch is a camera and not a list (ADR-090). It is consumed -- so this
    // asserts what the DIGEST did, not what the call returned, which is the thing that changed when
    // the map learned to zoom.
    (void)page.HandleZoom(-1, 700.0F, 300.0F);
    Assert::AreEqual(std::size_t{1}, page.DigestTop(), L"a notch over the map scrolled the digest");
    Assert::IsFalse(page.Map().AtAuthoredFraming(), L"and the map did not take it either, so the notch went nowhere");
    page.ResetView();

    Assert::IsTrue(page.HandleZoom(1, 100.0F, 300.0F), L"a notch the other way did not come back");
    Assert::AreEqual(std::size_t{0}, page.DigestTop());

    // And it stops at the top rather than banking notches that have to be spun back.
    Assert::IsFalse(page.HandleZoom(1, 100.0F, 300.0F));
    Assert::AreEqual(std::size_t{0}, page.DigestTop());
  }

  TEST_METHOD(ThePageKeysMoveAScreenfulAndNotACard)
  {
    // A page is however many cards fit, which only the layout knows -- so the key asks the frame
    // rather than a number chosen in the handler (ADR-080).
    Lockstep::MainPage page;
    page.Create(ATallDigest());

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    const std::size_t screenful = page.CardsOnScreen();
    Assert::IsTrue(screenful > 1, L"this fixture fits one card a screen, so a page and a card are the same move");

    Assert::IsTrue(page.HandleKey(Neuron::KeyboardInput::Key::PageDown));
    Assert::AreEqual(screenful, page.DigestTop(), L"PageDown moved by something other than a screenful");

    Assert::IsTrue(page.HandleKey(Neuron::KeyboardInput::Key::PageUp));
    Assert::AreEqual(std::size_t{0}, page.DigestTop(), L"PageUp did not come back");

    Assert::IsFalse(page.HandleKey(Neuron::KeyboardInput::Key::Escape), L"a key this screen does not use did something");
  }

  TEST_METHOD(ADragOverTheDigestScrollsItByWholeCards)
  {
    // The finger's half. A drag is continuous and the column moves in cards, so what is left over is
    // banked: without that a slow drag scrolls nothing at all.
    Lockstep::MainPage page;
    page.Create(ATallDigest());

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    const Neuron::PointerInput::Drag nudge{.deltaXPixels = 0.0F, .deltaYPixels = -10.0F, .originXPixels = 100.0F, .originYPixels = 300.0F};
    Assert::IsTrue(page.HandleDrag(nudge), L"a drag that began on the digest was not consumed by it");
    Assert::AreEqual(std::size_t{0}, page.DigestTop(), L"ten pixels moved a whole card");

    for (std::int32_t again = 0; again < 4; ++again)
    {
      (void)page.HandleDrag(nudge);
    }
    Assert::AreEqual(std::size_t{1}, page.DigestTop(), L"fifty pixels of drag banked no card at all");
  }

  TEST_METHOD(TheBandSaysWhatIsHiddenAndNotHowManyPages)
  {
    // `1 / 4 · MORE ›` told a player how much column was left and nothing about whether the battle
    // they had not seen was in it (ADR-080).
    const auto simulation = PlayedMatch(2);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.digest.clear();
    for (std::int32_t index = 0; index < 20; ++index)
    {
      AddEvent(state, Lockstep::NOBODY, std::format("Production +{}", index + 1));
    }

    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(state);
    Assert::IsTrue(cards.size() > 3U);

    const std::string summary = Lockstep::HiddenSummary(cards, 3);
    Assert::IsTrue(summary.find(std::to_string(cards.size() - 3)) != std::string::npos, L"the band does not say how many are hidden");
    Assert::IsTrue(summary.find("MORE") != std::string::npos);
    Assert::IsTrue(Lockstep::HiddenSummary(cards, cards.size()).empty(), L"a band with nothing below it still claimed something");
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
    Assert::IsTrue(OpenBuildSheet(page, renderers), L"nothing on the screen opened a build sheet");

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
    Assert::IsTrue(OpenBuildSheet(page, renderers), L"nothing on the screen opened a build sheet");

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
    Assert::IsTrue(OpenBuildSheet(page, renderers), L"nothing on the screen opened a build sheet");

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
    Assert::IsTrue(OpenBuildSheet(page, renderers, built.system), L"the building capital opened no build sheet");
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
    Assert::IsTrue(OpenBuildSheet(page, renderers, PositionOf(page.State(), laneSystem)), L"the lane's system opened no build sheet");

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

    // Sweep the whole screen, exactly as a finger would hunt for the control. Two stages, because
    // the order takes two taps: one to reach a picker, one to pick a lane.
    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Destination; });
    Assert::IsTrue(opened, L"nothing on the screen opens a destination picker for a fleet that is standing still");

    const std::int32_t where = page.State().fleets[static_cast<std::size_t>(standing)].from;
    const bool ordered = SweepFor(
      page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
      [&page, standing, where] { return page.State().fleets[static_cast<std::size_t>(standing)].to != where; }, true,
      [&page, &renderers]
      {
        // A stray tap on the map closes the picker over some other system's build sheet. Put it
        // back, so the sweep is looking for a row rather than for the panel it already found.
        if (page.OpenPanel() == Lockstep::MainPage::Panel::Destination)
        {
          return false;
        }
        return SweepFor(page, renderers, DrawPage, SCREEN_WIDTH - ORDERS_RAIL, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                        [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Destination; });
      });
    Assert::IsTrue(ordered, L"the picker opened and no row in it ordered the fleet anywhere");

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

  TEST_METHOD(ABadgeOpensThePickerForTheFleetStandingUnderIt)
  {
    // The map drew nothing at all for a parked fleet before this, so no tap in this pane could
    // reach a picker on a board with nothing in transit -- which is every board at tick zero.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Assert::IsTrue(WhereTheFleetStands(page.State()) != Lockstep::EventRefs::NONE, L"the opening board has no fleet standing anywhere");
    Assert::IsFalse(
      std::any_of(page.State().fleets.begin(), page.State().fleets.end(), [](const Lockstep::Fleet& _fleet) { return _fleet.OnALane(); }),
      L"something is in transit, so a marker rather than a badge could be what answers this");

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, MAP_LEFT, TOP_BAR, MAP_RIGHT, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Destination; });
    Assert::IsTrue(opened, L"nothing on the map opens a picker for a fleet standing at a system");
  }

  TEST_METHOD(ASystemHoldingSeveralAsksWhichOneFirst)
  {
    // A badge totals SHIPS, so a system holding three fleets wears one badge and the tap that
    // follows it has to be about one fleet. The sheet between them is that question.
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
    const bool listed = SweepFor(page, renderers, DrawPage, MAP_LEFT, TOP_BAR, MAP_RIGHT, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::FleetList; });
    Assert::IsTrue(listed, L"a system holding two of your fleets went somewhere other than the list that picks between them");

    // And the list leads on to a picker, which is the only thing it is for.
    const bool picked = SweepFor(
      page, renderers, DrawPage, MAP_LEFT, TOP_BAR, MAP_RIGHT, SCREEN_HEIGHT,
      [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Destination; }, true,
      [&page, &renderers]
      {
        if (page.OpenPanel() != Lockstep::MainPage::Panel::None)
        {
          return false;
        }
        return SweepFor(page, renderers, DrawPage, MAP_LEFT, TOP_BAR, MAP_RIGHT, SCREEN_HEIGHT,
                        [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::FleetList; });
      });
    Assert::IsTrue(picked, L"no row of the fleet list opens that fleet's picker");
  }

  TEST_METHOD(ARivalsGarrisonIsReadAndNotOrdered)
  {
    // A rival's badge says how strong a system is, which is the same fact ADR-063 puts on a
    // destination row. It is not a control: there is no order to give about somebody else's ships.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsFalse(state.fleets.empty());

    // Every fleet on the board belongs to a rival, so any picker this sweep finds is one the map
    // offered about ships that are not the viewer's.
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
    const bool opened = SweepFor(
      page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT, [&page]
      { return page.OpenPanel() == Lockstep::MainPage::Panel::Destination || page.OpenPanel() == Lockstep::MainPage::Panel::FleetList; });
    Assert::IsFalse(opened, L"the screen offered an order about a rival's ships");
  }

  TEST_METHOD(ABadgeIsFocusOnlyAtTheLock)
  {
    // Screen 06, and the rule the locks rail's rows already follow (ADR-060): at the lock nothing
    // opens a surface that takes an order for a tick that is already resolving.
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.orders.locked = true;

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    const bool opened = SweepFor(
      page, renderers, DrawPage, MAP_LEFT, TOP_BAR, MAP_RIGHT, SCREEN_HEIGHT, [&page]
      { return page.OpenPanel() == Lockstep::MainPage::Panel::Destination || page.OpenPanel() == Lockstep::MainPage::Panel::FleetList; });
    Assert::IsFalse(opened, L"a badge opened a picker at the lock");
    Assert::IsTrue(page.FocusedSystem() != Lockstep::EventRefs::NONE, L"and it focused nothing either, so the tap did nothing at all");
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
TEST_CLASS(OfflineBoardTapTests)
{
public:
  TEST_METHOD(NoTapGivesAnOrderWhileTheLinkIsDown)
  {
    // The same claim `ALockedRailQueuesNothing` makes about the lock, about the other reason an
    // order cannot go: a client that cannot send must not let one be composed either, or the player
    // is editing a list that will never leave.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    page.SetOffline(true);
    Assert::IsFalse(page.OrdersEditable(), L"a page with no link still says orders can be given");

    Headless renderers;
    const bool ordered =
      SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
               [&page]
               {
                 const Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
                 return !orders.builds.empty() || !orders.fleetOrders.empty() || !orders.proposals.empty() || orders.concede;
               });
    Assert::IsFalse(ordered, L"a tap composed an order on a client that cannot send one");
  }

  TEST_METHOD(TheBoardIsStillReadableWhileTheLinkIsDown)
  {
    // The half the modal took away. Focusing, opening a sheet and reading the digest reach no
    // socket, so none of them is a thing to stop.
    const auto simulation = PlayedMatch(4);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    page.SetOffline(true);

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() != Lockstep::MainPage::Panel::None; });
    Assert::IsTrue(opened, L"a dropped link left nothing on the board to open");

    Lockstep::MainPage focusing;
    focusing.Create(ViewOfSeatZero(*simulation));
    focusing.SetOffline(true);
    const bool focused = SweepFor(focusing, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                  [&focusing] { return focusing.FocusedSystem() != Lockstep::EventRefs::NONE; });
    Assert::IsTrue(focused, L"a dropped link left nothing on the board to focus");
  }

  TEST_METHOD(ComingBackRestoresTheControls)
  {
    // It is a state and not a one-way door: the reconnect loop under the banner is expected to win.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    page.SetOffline(true);
    Assert::IsFalse(page.OrdersEditable());
    page.SetOffline(false);
    Assert::IsTrue(page.OrdersEditable(), L"the controls did not come back with the link");
  }
};

// The top bar carries the purse AND what this tick has committed of it (ADR-087).
TEST_CLASS(CommittedPurseTests)
{
public:
  TEST_METHOD(NothingQueuedCommitsNothing)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    Assert::AreEqual(0u, page.State().orders.QueuedBuildCost(), L"a fresh match has something queued already");
  }

  TEST_METHOD(TheCommittedAmountIsWhatTheSheetPricesAgainst)
  {
    // One number, read two ways: the bar subtracts it and the sheet says so in a sentence. They are
    // the same call, so they cannot disagree (ADR-078, ADR-087).
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsFalse(state.orders.builds.empty());

    state.player.credits = 46;
    state.orders.builds.front().cost = 20;
    state.orders.queuedBuilds.push_back(0);

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Assert::AreEqual(20u, page.State().orders.QueuedBuildCost(), L"the bar would subtract the wrong number");
    Assert::IsTrue(page.PurseSentence().find("26") != std::string::npos, L"and the sheet would not agree with it");
  }
};

// The map has a camera the player can move, and a way back to where it started (ADR-090).
TEST_CLASS(MapCameraTapTests)
{
public:
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
};

// The top bar says what it knows and hides what is not finished (ADR-091).
TEST_CLASS(TopBarTests)
{
public:
  TEST_METHOD(ReplayIsOffTheBarUnlessDeveloperControlsAreOn)
  {
    // Its sheet is a stub, and a control whose own title said `NOT YET WIRED` teaches a player that
    // the buttons on this screen may do nothing -- which is what ADR-053 and ADR-077 were spent
    // unteaching.
    const auto simulation = PlayedMatch(2);
    Lockstep::MainPage shipped;
    shipped.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    const bool found = SweepFor(shipped, renderers, DrawPage, 0, 0, SCREEN_WIDTH, TOP_BAR,
                                [&shipped] { return shipped.OpenPanel() == Lockstep::MainPage::Panel::Replay; });
    Assert::IsFalse(found, L"a shipped build put REPLAY on the bar");

    Lockstep::MainPage dev;
    dev.Create(ViewOfSeatZero(*simulation));
    dev.SetDeveloperControls(true);
    const bool reachable = SweepFor(dev, renderers, DrawPage, 0, 0, SCREEN_WIDTH, TOP_BAR,
                                    [&dev] { return dev.OpenPanel() == Lockstep::MainPage::Panel::Replay; });
    Assert::IsTrue(reachable, L"--dev did not put REPLAY back");
  }

  TEST_METHOD(TheCensusNamesNoMatchId)
  {
    // `M0007` was the zero-padded TICK, which names neither the match nor the tick and changes every
    // tick while looking like an identifier.
    const auto simulation = PlayedMatch(7);
    const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    // The id the view model carries is still the tick; what changed is that the bar stops printing
    // it. Asserted through the state rather than the pixels, because the draw is a capture.
    Assert::AreEqual(state.match.tick, static_cast<std::uint32_t>(std::stoul(state.match.id)),
                     L"the match id stopped being the tick, so the bar may be able to show one after all");
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

  TEST_METHOD(ADestinationSheetPutsTheNearestFirst)
  {
    // Lane order is the order the graph happens to store them in and means nothing to a player; how
    // soon a fleet lands is the first thing they weigh.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, SCREEN_WIDTH - ORDERS_RAIL, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() == Lockstep::MainPage::Panel::Destination; });
    Assert::IsTrue(opened, L"no picker to check the order of");

    // The sheet is drawing; what it drew is a capture. What can be asserted here is the rule the
    // sort is built on -- the lanes out of the fleet's system, in cost order, are what the rows are.
    const std::int32_t standing = page.State().fleets.front().from;
    std::vector<std::uint32_t> costs;
    for (const Lockstep::Lane& lane : page.State().graph.lanes)
    {
      if (lane.a == standing || lane.b == standing)
      {
        costs.push_back(lane.cost);
      }
    }
    Assert::IsFalse(costs.empty(), L"the fleet's system has no lanes, so there is nothing to sort");
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
    Assert::IsTrue(below->text.find("BUILDS") != std::string::npos || below->text.find("SIGNALS") != std::string::npos,
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
