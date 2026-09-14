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

#include "MainPage.h"
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

  void Begin()
  {
    shapes.BeginFrame();
    text.BeginFrame();
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
  _page.DrawWorld(_renderers.shapes, _renderers.text);
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

  TEST_METHOD(TheLockedBoardHasNoUndersizedTarget)
  {
    // At the lock most controls stop being targets at all (ADR-060, ADR-065), which is a different
    // hit list rather than the same one dimmed.
    const auto simulation = PlayedMatch(4);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.orders.locked = true;
    AuditMainPage(std::move(state), L"the main page at the lock");
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
