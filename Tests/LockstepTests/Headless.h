#pragma once

// Headless.h -- the buttons, pressed by something other than a finger: the harness every tap suite
// sweeps with.
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

#include "ConnectionDialog.h"
#include "MainPage.h"
#include "SeatsPage.h"
#include "SnapshotView.h"

#include "BotPolicy.h"
#include "MatchSimulation.h"
#include "TickResolver.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace LockstepTests
{

inline constexpr std::int32_t SCREEN_WIDTH = 1280;
inline constexpr std::int32_t SCREEN_HEIGHT = 720;

/// How far apart the sweep's taps are. Eight pixels is the glyph box and smaller than any control
/// on any of these screens, so nothing can hide between two of them.
///
/// Whole pixels, and the sweeps below count in them rather than in floats. A tap IS a pixel -- the
/// pointer reports integers -- and a float counter accumulating a step is a loop whose last
/// iteration is somewhere nobody chose.
inline constexpr std::int32_t STEP = 8;

/// The two bands a sweep is sometimes confined to: the top bar, which is not what any of these
/// tests are about, and the orders rail down the right-hand side.
inline constexpr std::int32_t TOP_BAR = 44;
inline constexpr std::int32_t ORDERS_RAIL = 260;

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

inline void DrawDialog(Lockstep::ConnectionDialog& _dialog, Headless& _renderers)
{
  _renderers.Begin();
  _dialog.Draw(_renderers.shapes, _renderers.text);
}

/// A dialog showing one kind, with the facts it needs.
[[nodiscard]] inline Lockstep::ConnectionDialog::Action PressSomething(Lockstep::ConnectionDialog::Kind _kind,
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

[[nodiscard]] inline std::unique_ptr<Lockstep::MatchSimulation> PlayedMatch(std::int32_t _ticks)
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

[[nodiscard]] inline Lockstep::MatchState ViewOfSeatZero(const Lockstep::MatchSimulation& _simulation)
{
  const Lockstep::PlayerId seat{0};
  return Lockstep::ViewOf(Lockstep::Snapshot::For(_simulation.State(), seat), Lockstep::Snapshot::DigestFor(_simulation.LastTick(), seat),
                          600);
}

/// Seat zero with two offers in front of it, one from each of two rivals.
///
/// `HoldForTicks` because it is the offer with no board precondition (SignalTests says the same of
/// its own helper); what these tests are about is the buttons, not how the offer got there.
[[nodiscard]] inline Lockstep::MatchState SeatZeroWithTwoOffers()
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

/// One number for everything a frame recorded into the shape recorder: every vertex's position and
/// colour, folded together.
///
/// **It is how a test says "the same picture" about something that has no strings in it.** The move
/// mode's ring is an alpha that breathes and its lanes are dashes that march, so a frame that
/// differs from the one before it differs in vertices and in nothing else. Drained a fixed number of
/// times rather than until empty, because `EndLayer` puts a boundary in the middle of the map's own
/// recording and a take that reaches one comes back empty with more behind it.
[[nodiscard]] inline std::uint64_t ShapeFingerprint(Neuron::ShapeRenderer& _shapes)
{
  constexpr std::uint64_t PRIME = 1099511628211ULL;
  std::uint64_t folded = 14695981039346656037ULL;
  for (std::int32_t drain = 0; drain < 4; ++drain)
  {
    for (const Neuron::ShapeRenderer::ShapeVertex& vertex : _shapes.TakeUnflushed().vertices)
    {
      folded = (folded ^ vertex.color) * PRIME;
      folded = (folded ^ static_cast<std::uint64_t>(std::lround(vertex.positionXPixels * 16.0F))) * PRIME;
      folded = (folded ^ static_cast<std::uint64_t>(std::lround(vertex.positionYPixels * 16.0F))) * PRIME;
    }
  }
  return folded;
}

inline void DrawPage(Lockstep::MainPage& _page, Headless& _renderers)
{
  _page.DrawWorld(_renderers.shapes, _renderers.text, _renderers.meshes);
  _page.DrawInterface(_renderers.shapes, _renderers.text);
}

inline void DrawSeats(Lockstep::SeatsPage& _page, Headless& _renderers)
{
  _page.DrawWorld(_renderers.shapes, _renderers.text);
  _page.DrawInterface(_renderers.shapes, _renderers.text);
}

/// Where system `_id` sits in the view's own fogged list, which is the index every `OpenSystem`
/// carries (ADR-057). `EventRefs::NONE` when the viewer cannot see it.
[[nodiscard]] inline std::int32_t PositionOf(const Lockstep::MatchState& _state, std::int32_t _id)
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

/// The open sheet's rectangle, read off the two rectangles that CLOSE it: the header's square
/// corner and the full-width bar under it (ADR-052). Zero-sized when no sheet is open.
///
/// **A control has to be told from the map under it by geometry, not by its action.** The map draws
/// a marker for a fleet with a move queued and that marker opens the same picker the sheet's row
/// does (ADR-055, ADR-077), so both are `OpenFleet` in the same pane -- and a test looking for the
/// sheet's control by action alone finds whichever of them the draw happened to record first.
[[nodiscard]] inline Lockstep::Frame::Box SheetBoundsOf(const Lockstep::MainPage& _page)
{
  Lockstep::Frame::Box box{0.0F, 0.0F, 0.0F, 0.0F};
  for (const Lockstep::MainPage::HitRegion& hit : _page.Hits())
  {
    if (hit.action != Lockstep::MainPage::Action::ClosePanel)
    {
      continue;
    }
    if (hit.width < hit.height * 2.0F)
    {
      box.y = hit.y;
    }
    else
    {
      box.x = hit.x;
      box.width = hit.width;
      box.height = hit.y + hit.height - box.y;
    }
  }
  return box;
}

/// Every control ON THE OPEN SHEET that is about a fleet: the `MOVE` on one that is standing, and
/// the `TAKE BACK` on one with a move queued (ADR-112).
[[nodiscard]] inline std::vector<Lockstep::MainPage::HitRegion> FleetControlsOn(const Lockstep::MainPage& _page)
{
  const Lockstep::Frame::Box sheet = SheetBoundsOf(_page);
  std::vector<Lockstep::MainPage::HitRegion> found;
  // **Read backwards, which is the order `HandleTap` resolves in**: the thing drawn last wins, and
  // the map's own fleet marker is drawn before the sheet that covers it.
  const std::vector<Lockstep::MainPage::HitRegion>& hits = _page.Hits();
  for (auto hit = hits.rbegin(); hit != hits.rend(); ++hit)
  {
    const bool aboutAFleet =
      hit->action == Lockstep::MainPage::Action::BeginMove || hit->action == Lockstep::MainPage::Action::CancelFleetOrder;
    const bool onTheSheet = hit->x >= sheet.x && hit->x < sheet.x + sheet.width && hit->y >= sheet.y && hit->y < sheet.y + sheet.height;
    const bool already =
      std::any_of(found.begin(), found.end(), [&hit](const Lockstep::MainPage::HitRegion& _found) { return _found.index == hit->index; });
    if (aboutAFleet && onTheSheet && !already)
    {
      found.push_back(*hit);
    }
  }
  return found;
}

/// Opens a PLACE sheet by pressing what opens one, and leaves the page drawn so its hit list is the
/// sheet's (ADR-058, ADR-107, ADR-112). `_system` is a position, or `NONE` for whichever opens
/// first.
///
/// It presses rather than setting a field for the reason every test in this file does: what is
/// under test is the sheet the screen builds, and a sheet reached by any other route is a different
/// sheet. A tap that opens nothing focuses instead, which is why every candidate is tried.
[[nodiscard]] inline bool OpenPlaceSheet(Lockstep::MainPage& _page, Headless& _renderers, std::int32_t _system = Lockstep::EventRefs::NONE)
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
    if (_page.OpenPanel() == Lockstep::MainPage::Panel::Place)
    {
      return true;
    }
  }
  return false;
}

/// Every tile on the open sheet that is a target, as the rectangles `AddHit` recorded.
///
/// **`ToggleBuild` is not enough to identify one.** Two other controls take the same order back: the
/// digest's priced build button used to place it from the other side of the screen (ADR-053), and
/// the locks rail's `×` takes a queued one back from the right-hand column (ADR-113). That is
/// correct and is why the guard behind all three is one. What tells them apart is the column: a
/// sheet is drawn over the map pane and never into a rail.
[[nodiscard]] inline std::vector<Lockstep::MainPage::HitRegion> TilesOn(const Lockstep::MainPage& _page)
{
  std::vector<Lockstep::MainPage::HitRegion> tiles;
  for (const Lockstep::MainPage::HitRegion& hit : _page.Hits())
  {
    const bool onTheMapPane = hit.x >= Lockstep::Frame::DIGEST_WIDTH && hit.x < SCREEN_WIDTH - ORDERS_RAIL;
    if (hit.action == Lockstep::MainPage::Action::ToggleBuild && onTheMapPane)
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

[[nodiscard]] inline BuildingCapital CapitalThatIsBuilding()
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

} // namespace LockstepTests
