// TouchTargetTests.cpp -- every tappable rectangle, measured against the floor.
//
// **This game is for touch and its copy always said so** (ADR-098): every player-facing string reads
// *tap*, `PointerInput` is built on the Windows Pointer API so a finger and a mouse arrive through
// one channel, and `MainPage::SHEET_ROW_HEIGHT` is 44 with a comment calling it the smallest a
// finger hits reliably. It was applied to sheet rows and to nothing else -- the three controls a
// player uses most were 18, 21 and 16.
//
// **Reading the constants is not evidence.** A control's box is composed from several of them --
// a line height, a padding, a band -- and the one that went wrong is always the one nobody added
// up. So this draws the real screens through the real recorder and measures what `AddHit` actually
// recorded, which is the same list `HandleTap` tests against.
//
// A failure here names the action, so the offender is findable without a coordinate.

#include "pch.h"
#include "CppUnitTest.h"

#include "ConnectionDialog.h"
#include "JoinPage.h"
#include "MainPage.h"
#include "SeatsPage.h"
#include "SnapshotView.h"

#include "BotPolicy.h"
#include "MatchSimulation.h"

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

namespace
{

/// The floor, owner decision 2026-09-14 (ADR-098). One number, and it is the one the tree already
/// names: 32 satisfies neither a finger nor `SHEET_ROW_HEIGHT`'s own comment.
constexpr float FLOOR_PIXELS = 44.0F;

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

[[nodiscard]] const char* NameOf(Lockstep::MainPage::Action _action)
{
  switch (_action)
  {
  case Lockstep::MainPage::Action::FocusEvent:
    return "FocusEvent";
  case Lockstep::MainPage::Action::FocusSystem:
    return "FocusSystem";
  case Lockstep::MainPage::Action::OpenSystem:
    return "OpenSystem";
  case Lockstep::MainPage::Action::OpenFleet:
    return "OpenFleet";
  case Lockstep::MainPage::Action::OpenFleetsAt:
    return "OpenFleetsAt";
  case Lockstep::MainPage::Action::ToggleBuild:
    return "ToggleBuild";
  case Lockstep::MainPage::Action::OpenSignals:
    return "OpenSignals";
  case Lockstep::MainPage::Action::ToggleSignal:
    return "ToggleSignal";
  case Lockstep::MainPage::Action::AcceptProposal:
    return "AcceptProposal";
  case Lockstep::MainPage::Action::DeclineProposal:
    return "DeclineProposal";
  case Lockstep::MainPage::Action::ToggleActorCard:
    return "ToggleActorCard";
  case Lockstep::MainPage::Action::ShowDigestPage:
    return "ShowDigestPage";
  case Lockstep::MainPage::Action::OpenReplay:
    return "OpenReplay";
  case Lockstep::MainPage::Action::ChooseDestination:
    return "ChooseDestination";
  case Lockstep::MainPage::Action::ResetCamera:
    return "ResetCamera";
  case Lockstep::MainPage::Action::ClosePanel:
    return "ClosePanel";
  case Lockstep::MainPage::Action::None:
  default:
    return "None";
  }
}

/// Every target on one drawn frame that is under the floor, as `Action@heightxwidth`.
[[nodiscard]] std::vector<std::string> Undersized(const Lockstep::MainPage& _page)
{
  std::vector<std::string> offenders;
  for (const Lockstep::MainPage::HitRegion& hit : _page.Hits())
  {
    // **Both dimensions.** A 260x21 rail row and a 15x16 garrison badge fail for different reasons,
    // and a floor that only watched height would have passed the badge at 15 wide.
    if (hit.height + 0.01F < FLOOR_PIXELS || hit.width + 0.01F < FLOOR_PIXELS)
    {
      offenders.push_back(std::format("{} {}x{}", NameOf(hit.action), hit.width, hit.height));
    }
  }
  return offenders;
}

[[nodiscard]] std::wstring Listed(std::string_view _what, const std::vector<std::string>& _offenders)
{
  std::string message{_what};
  message += std::format(" ({} of them):", _offenders.size());
  for (const std::string& offender : _offenders)
  {
    message += "\n  " + offender;
  }
  return std::wstring{message.begin(), message.end()};
}

void DrawPage(Lockstep::MainPage& _page, Headless& _renderers)
{
  _renderers.Begin();
  _page.DrawWorld(_renderers.shapes, _renderers.text, _renderers.meshes);
  _page.DrawInterface(_renderers.shapes, _renderers.text);
}

/// One page, drawn in a state, audited.
void AuditMainPage(Lockstep::MatchState _state, const wchar_t* _what)
{
  Headless renderers;
  Lockstep::MainPage page;
  page.Create(std::move(_state));
  DrawPage(page, renderers);

  const std::vector<std::string> offenders = Undersized(page);
  if (!offenders.empty())
  {
    Assert::Fail((std::wstring{_what} + L": targets under the 44px floor. " + Listed("undersized", offenders)).c_str());
  }
}

/// The same audit over any page's hit list, with the action printed by whatever names it.
///
/// **Three more pages, and they are not one page's problem.** `TouchTargetTests` walked the main
/// page only, because that is where ADR-098's three worst offenders were. The join screen, the
/// lobby and the connection dialog are the first three screens a player ever touches and their
/// controls were never measured at all.
template <typename Hits, typename Name> [[nodiscard]] std::vector<std::string> UndersizedIn(const Hits& _hits, Name _name)
{
  std::vector<std::string> offenders;
  for (const auto& hit : _hits)
  {
    if (hit.height + 0.01F < FLOOR_PIXELS || hit.width + 0.01F < FLOOR_PIXELS)
    {
      offenders.push_back(std::format("{} {}x{}", _name(hit.action), hit.width, hit.height));
    }
  }
  return offenders;
}

void AuditHits(const std::vector<std::string>& _offenders, const wchar_t* _what)
{
  if (!_offenders.empty())
  {
    Assert::Fail((std::wstring{_what} + L": targets under the 44px floor. " + Listed("undersized", _offenders)).c_str());
  }
}

} // namespace

TEST_CLASS(TouchTargetTests)
{
public:
  TEST_METHOD(TheOpeningBoardHasNoUndersizedTarget)
  {
    const auto simulation = PlayedMatch(0);
    AuditMainPage(ViewOfSeatZero(*simulation), L"the main page at tick 0");
  }

  TEST_METHOD(APlayedBoardHasNoUndersizedTarget)
  {
    // Four ticks in: a digest with cards and buttons, a rail with fleets and builds, a map with
    // garrison badges. The state the three worst offenders live in.
    const auto simulation = PlayedMatch(4);
    AuditMainPage(ViewOfSeatZero(*simulation), L"the main page at tick 4");
  }

  TEST_METHOD(ASheetOverTheBoardHasNoUndersizedTarget)
  {
    // Sheets are 44 by construction (ADR-052) -- what this catches is the header's close corner,
    // the `CANCEL` bar and the band between them, which are composed from different numbers.
    const auto simulation = PlayedMatch(4);
    Headless renderers;
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    // Open whatever will open. **Every candidate, not the first** -- `OpenSystem` on a rival's
    // system focuses and opens nothing (ADR-058), so taking the first one found is a coin flip.
    DrawPage(page, renderers);
    const std::vector<Lockstep::MainPage::HitRegion> candidates = page.Hits();
    for (const Lockstep::MainPage::HitRegion& hit : candidates)
    {
      const bool opener = hit.action == Lockstep::MainPage::Action::OpenFleet || hit.action == Lockstep::MainPage::Action::OpenFleetsAt ||
                          hit.action == Lockstep::MainPage::Action::OpenSystem || hit.action == Lockstep::MainPage::Action::OpenSignals;
      if (!opener)
      {
        continue;
      }
      (void)page.HandleTap(hit.x + hit.width * 0.5F, hit.y + hit.height * 0.5F);
      if (page.OpenPanel() != Lockstep::MainPage::Panel::None)
      {
        break;
      }
      DrawPage(page, renderers);
    }
    Assert::IsTrue(page.OpenPanel() != Lockstep::MainPage::Panel::None, L"nothing opened, so no sheet was audited");

    DrawPage(page, renderers);
    const std::vector<std::string> offenders = Undersized(page);
    Assert::IsTrue(offenders.empty(), Listed("a sheet is open and these targets are under the floor", offenders).c_str());
  }

  TEST_METHOD(ABuildSheetsTilesAndCloseCornerAreAtTheFloor)
  {
    // The build sheet is the one that is a GRID (ADR-107), so its rectangles are composed from
    // different numbers than a row's: a tile's width is the sheet's less two paddings and a gap,
    // halved, and its height is stated. Both are well over the floor by construction -- which is
    // exactly the kind of claim this file exists to measure rather than to read off a constant.
    const auto simulation = PlayedMatch(0);
    Headless renderers;
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    DrawPage(page, renderers);
    const std::vector<Lockstep::MainPage::HitRegion> candidates = page.Hits();
    for (const Lockstep::MainPage::HitRegion& hit : candidates)
    {
      if (hit.action != Lockstep::MainPage::Action::OpenSystem || hit.index < 0)
      {
        continue;
      }
      (void)page.HandleTap(hit.x + hit.width * 0.5F, hit.y + hit.height * 0.5F);
      DrawPage(page, renderers);
      if (page.OpenPanel() == Lockstep::MainPage::Panel::Place)
      {
        break;
      }
    }
    Assert::IsTrue(page.OpenPanel() == Lockstep::MainPage::Panel::Place, L"no build sheet opened, so nothing was audited");

    const std::vector<std::string> offenders = Undersized(page);
    Assert::IsTrue(offenders.empty(), Listed("a build sheet is open and these targets are under the floor", offenders).c_str());

    // The close corner is the header's own height, in both dimensions: 36 until ADR-100 grew it,
    // and the one target on a sheet that is a square rather than a bar.
    std::size_t corners = 0;
    std::size_t tiles = 0;
    for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
    {
      // Two things close a sheet and they are told apart by shape: the `X` is the header's own
      // square corner and `CANCEL` is a bar the width of the sheet (ADR-052).
      if (hit.action == Lockstep::MainPage::Action::ClosePanel && hit.width < hit.height * 2.0F)
      {
        Assert::AreEqual(FLOOR_PIXELS, hit.width, 0.01F, L"the sheet's close corner is not 44 wide");
        Assert::AreEqual(FLOOR_PIXELS, hit.height, 0.01F, L"the sheet's close corner is not 44 tall");
        ++corners;
      }
      // A tile, and not the digest's priced build button, which queues the same order from the
      // other column and so carries the same action (ADR-053). The sheet is over the map pane.
      if (hit.action == Lockstep::MainPage::Action::ToggleBuild && hit.x >= Lockstep::Frame::DIGEST_WIDTH)
      {
        Assert::IsTrue(hit.height >= 96.0F, L"a build tile is shorter than the 96 it is drawn at");
        ++tiles;
      }
    }
    Assert::AreEqual(std::size_t{1}, corners, L"the sheet has no close corner, or more than one");
    Assert::IsTrue(tiles > 0, L"the sheet drew no tile, so the tile measurement asserted nothing");
  }

  TEST_METHOD(ADigestButtonIsDrawnAtTwentyEightAndTappedAtFortyFour)
  {
    // **The one control on this page that is deliberately SMALLER than the floor** (ADR-110). A
    // button's box is 28 and its target is grown to 44 around it, which is ADR-100's isolated-chip
    // rule rather than its column one -- so the claim worth measuring is not "nothing is under the
    // floor", which the sweeps above already make, but that the grow is happening at all. A button
    // box that quietly became 44 again would pass every other test in this file.
    Assert::IsTrue(Lockstep::MainPage::BUTTON_HEIGHT < FLOOR_PIXELS, L"the button box is no longer under the floor, so nothing is grown");
    Assert::AreEqual(FLOOR_PIXELS, Lockstep::MainPage::BUTTON_HEIGHT + 2.0F * Lockstep::MainPage::BUTTON_GAP, 0.01F,
                     L"a button and its two gaps no longer come to the floor, so two grown targets can overlap");

    const auto simulation = PlayedMatch(0);
    Headless renderers;
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    DrawPage(page, renderers);

    // The standing `BUILD AT DOTHAN` the opening digest carries (ADR-056), in the digest column. It
    // is an `OpenSystem` since ADR-111 -- no digest control places an order -- and the map's own
    // discs carry the same action from over the pane, which is what the column test excludes.
    std::size_t buttons = 0;
    for (const Lockstep::MainPage::HitRegion& hit : page.Hits())
    {
      if (hit.action != Lockstep::MainPage::Action::OpenSystem || hit.x >= Lockstep::Frame::DIGEST_WIDTH)
      {
        continue;
      }
      Assert::AreEqual(FLOOR_PIXELS, hit.height, 0.01F, L"a digest button's target is not the floor exactly");
      Assert::IsTrue(hit.width + 0.01F >= FLOOR_PIXELS, L"a digest button's target is narrower than the floor");
      ++buttons;
    }
    Assert::IsTrue(buttons > 0, L"the opening digest drew no link to a place, so nothing was measured");
  }

  TEST_METHOD(TheLockedBoardHasNoUndersizedTarget)
  {
    // At the lock most controls stop being targets at all (ADR-060, ADR-065), which is a different
    // hit list rather than the same one dimmed.
    const auto simulation = PlayedMatch(4);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.orders.locked = true;
    AuditMainPage(std::move(state), L"the main page at the lock");
  }

  TEST_METHOD(TheJoinScreenHasNoUndersizedTarget)
  {
    // The first screen anybody sees, and the one a player who typed a token wrong comes back to.
    // Two fields, `HIDE` and `JOIN`.
    Headless renderers;
    Lockstep::JoinPage page;
    page.Offer("127.0.0.1:7341", "5H7K-K2MU");

    renderers.Begin();
    page.DrawWorld(renderers.shapes, renderers.text);
    page.DrawInterface(renderers.shapes, renderers.text);

    AuditHits(UndersizedIn(page.Hits(), [](std::int32_t _action) { return std::format("join action {}", _action); }), L"the join screen");
  }

  TEST_METHOD(TheLobbyHasNoUndersizedTarget)
  {
    // Six seat cards with a three-way toggle each, the detail panel's `COPY` and `NEW TOKEN`, the
    // bot styles, `PRACTICE MATCH`, and the footer's two.
    Headless renderers;
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};

    renderers.Begin();
    page.DrawWorld(renderers.shapes, renderers.text);
    page.DrawInterface(renderers.shapes, renderers.text);

    AuditHits(UndersizedIn(page.Hits(), [](std::int32_t _action) { return std::format("lobby action {}", _action); }), L"the lobby");
  }

  TEST_METHOD(TheConnectionDialogHasNoUndersizedTarget)
  {
    // Its buttons are the only way out of a screen that has nothing else on it, which makes them
    // the targets it is worst to miss.
    Headless renderers;
    Lockstep::ConnectionDialog dialog;
    dialog.Update(Lockstep::ConnectionDialog::Kind::Lost, Lockstep::ConnectionDialog::Facts{}, 0.0);

    renderers.Begin();
    dialog.Draw(renderers.shapes, renderers.text);

    AuditHits(UndersizedIn(dialog.Hits(), [](Lockstep::ConnectionDialog::Action _action)
                           { return std::format("dialog action {}", static_cast<std::int32_t>(_action)); }),
              L"the connection dialog");
  }

  TEST_METHOD(TheDeveloperBarHasNoUndersizedTarget)
  {
    // `REPLAY` ships hidden (ADR-091) and is still a control somebody presses.
    const auto simulation = PlayedMatch(4);
    Headless renderers;
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    page.SetDeveloperControls(true);
    DrawPage(page, renderers);

    const std::vector<std::string> offenders = Undersized(page);
    Assert::IsTrue(offenders.empty(), Listed("--dev is on and these targets are under the floor", offenders).c_str());
  }
};

} // namespace LockstepTests
