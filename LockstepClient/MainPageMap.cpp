// MainPageMap.cpp -- the map pane: the world pass, the bounds the camera frames, the lane arithmetic.
//
// The map itself is `MapRender` (ADR-014, ADR-017); this unit is what the page owns around it -- the
// frame it hands over, the content it measures so the camera can frame it, and the tick distance
// between two systems, which the move mode and the rail both ask for.

#include "pch.h"
#include "MainPage.h"

#include "DesignTokens.h"
#include "MapRender.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>

namespace Lockstep
{

namespace
{

using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

/// What a tap does, which the rows composed in here have to name (ADR-113). The page's own enum,
/// aliased rather than qualified thirty times.
using Action = MainPage::Action;

/// Node geometry, in WORLD units now rather than as multiples of a depth scale: the camera turns
/// a world size into a screen size, which is what makes a system genuinely larger when it is
/// nearer (ADR-017). The numbers are the reference's, read as world units.
constexpr float REGION_RADIUS = 62.0F;

/// How high the sealed region's second ring floats. The reference lifted it by the ellipse's own
/// half-height; in world units that is about a quarter of the radius.
constexpr float REGION_VOLUME_HEIGHT = 26.0F;

} // namespace

void MainPage::MeasureContent()
{
  // What the camera has to fit: every system, and the sealed region out to its rim. Measured
  // once, from the graph, because the galaxy the server sends will not be the one in the fixture
  // and a distance tuned to this sample would frame nothing else (ADR-017).
  float minX = 0.0F;
  float maxX = 0.0F;
  float minY = 0.0F;
  float maxY = 0.0F;
  bool first = true;

  for (std::size_t index = 0; index < m_state.graph.systems.size(); ++index)
  {
    const SystemNode& node = m_state.graph.systems[index];
    // The region anchor draws no node but the region around it is 62 units wide, so it counts for
    // its rim rather than for its centre.
    const float reach = HasFlag(node.flags, SystemFlags::RegionAnchor) ? REGION_RADIUS : 0.0F;

    if (first)
    {
      minX = node.positionX - reach;
      maxX = node.positionX + reach;
      minY = node.positionY - reach;
      maxY = node.positionY + reach;
      first = false;
      continue;
    }
    minX = std::min(minX, node.positionX - reach);
    maxX = std::max(maxX, node.positionX + reach);
    minY = std::min(minY, node.positionY - reach);
    maxY = std::max(maxY, node.positionY + reach);
  }

  if (first)
  {
    m_contentCenter = {0.0F, 0.0F, 0.0F};
    m_contentRadius = 1.0F;
    return;
  }

  const float centerDesignX = (minX + maxX) * 0.5F;
  const float centerDesignY = (minY + maxY) * 0.5F;
  m_contentCenter = MapView::Ground(centerDesignX, centerDesignY);

  const float halfWidth = (maxX - minX) * 0.5F;
  const float halfDepth = (maxY - minY) * 0.5F;
  // The radius IN THE GROUND PLANE, which is what makes the framing independent of yaw: spin the
  // camera and the content is exactly this wide from every direction. Height is passed separately,
  // because a galaxy is flat and pretending otherwise wastes a quarter of the pane (MapView.h).
  m_contentRadius = std::sqrt(halfWidth * halfWidth + halfDepth * halfDepth);
}

void MainPage::FocusOn(std::int32_t _system) noexcept
{
  // **A position is only a system while this snapshot is the one it came from** (ADR-057), so an
  // index the graph does not have focuses nothing rather than being stored and drawn against.
  const bool known = _system >= 0 && _system < static_cast<std::int32_t>(m_state.graph.systems.size());
  m_focusedSystem = known ? _system : EventRefs::NONE;

  if (!known)
  {
    m_mapView.AimAtContent();
    return;
  }

  // The GROUND under it, not the top of its stem: a station's height is its production (ADR-103),
  // so aiming at the ball would put a rich system's neighbours lower in the pane than a poor one's
  // and the centring would read as a wobble rather than as a rule.
  const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(_system)];
  m_mapView.AimAt(MapView::Ground(node.positionX, node.positionY));
}

std::uint32_t MainPage::TicksTo(std::int32_t _fromSystem, std::int32_t _toSystem) const
{
  // Dijkstra over lane costs. Lane cost is authored per edge and is not a distance (one-pager),
  // so a shortest path here is a shortest TIME and that is what the picker shows.
  constexpr std::uint32_t UNREACHABLE = std::numeric_limits<std::uint32_t>::max();
  if (_fromSystem < 0 || _toSystem < 0)
  {
    return UNREACHABLE;
  }

  std::vector<std::uint32_t> best(m_state.graph.systems.size(), UNREACHABLE);
  using Entry = std::pair<std::uint32_t, std::int32_t>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> frontier;

  best[static_cast<std::size_t>(_fromSystem)] = 0;
  frontier.emplace(0U, _fromSystem);

  while (!frontier.empty())
  {
    const auto [cost, at] = frontier.top();
    frontier.pop();
    if (at == _toSystem)
    {
      return cost;
    }
    if (cost > best[static_cast<std::size_t>(at)])
    {
      continue;
    }

    for (const Lane& lane : m_state.graph.lanes)
    {
      std::int32_t other = EventRefs::NONE;
      if (lane.a == at)
      {
        other = lane.b;
      }
      else if (lane.b == at)
      {
        other = lane.a;
      }
      if (other == EventRefs::NONE)
      {
        continue;
      }

      const std::uint32_t through = cost + lane.cost;
      if (through < best[static_cast<std::size_t>(other)])
      {
        best[static_cast<std::size_t>(other)] = through;
        frontier.emplace(through, other);
      }
    }
  }

  return best[static_cast<std::size_t>(_toSystem)];
}

void MainPage::DrawWorld(ShapeRenderer& _shapes, FontRenderer& _text, Neuron::MeshRenderer& _meshes)
{
  m_hits.clear();
  // Cleared with the hit list rather than inside the rail, because the digest's buttons fill under
  // the pointer too and the rail is drawn after them (ADR-111).
  m_hoverRegions.clear();

  _shapes.FillRect(0.0F, 0.0F, Frame::SCREEN_WIDTH, Frame::SCREEN_HEIGHT, Ink::APP_BACKGROUND);

  // THE MAP GOES FIRST, and the rails are painted over it. With the authored curve the map could
  // not leave its pane; a camera can put a projected label or a lane anywhere on the screen, so the
  // rails' own opaque backgrounds are what confine it (ADR-017).
  //
  // It lives in `MapRender` now (ADR-045) and hands back what it drew rather than reaching into
  // this page's hit list: the map knows a system from a fleet, and this knows what tapping one
  // does.
  // Worked out before the frame is composed, because the map draws what it is handed and the rules
  // the lock would refuse an order by are the page's to know (ADR-114).
  std::vector<MoveTarget> moveTargets;
  if (m_moveMode.has_value())
  {
    for (const MoveTargetSystem& target : ReachableFor(m_moveMode->fleet))
    {
      moveTargets.push_back(MoveTarget{.system = target.system, .ticks = target.ticks, .arrivesAt = target.arrivesAt});
    }
  }

  // Asked once and used twice: it decides whether the legend is drawn at all, and whether a
  // centred system has to be lifted clear of what is covering the bottom of the pane (ADR-115).
  const bool sheetOpen = m_panel != Panel::None || m_moveMode.has_value();

  const MapFrame frame{.state = m_state,
                       .view = m_mapView,
                       .sky = m_sky,
                       .contentCenter = m_contentCenter,
                       .contentRadius = m_contentRadius,
                       .aimLiftPixels = sheetOpen ? FOCUS_LIFT_PIXELS : 0.0F,
                       .focusedSystem = m_focusedSystem,
                       .animationSeconds = m_animationSeconds,
                       .sheetOpen = sheetOpen,
                       .moveOrigin = m_moveMode.has_value() ? m_moveMode->origin : EventRefs::NONE,
                       .moveSelected = m_moveMode.has_value() ? m_moveMode->selected : EventRefs::NONE,
                       .moveTargets = moveTargets};

  const std::vector<MapHit> mapHits = Lockstep::DrawMap(_shapes, _text, _meshes, frame);

  // **`RESET` is drawn only when the camera is somewhere other than where the map opened**
  // (ADR-090). There has been no way back to the authored framing since the map got a camera
  // (ADR-017) -- `ResetView` existed and nothing called it -- and hunting for it by eye is not a
  // thing to ask. A control that would do nothing is left off the screen rather than drawn dim,
  // because the map pane has no chrome and one chip appearing is itself the signal.
  //
  // **Not while a move is being chosen** (ADR-114, ADR-115). Two reasons, and either is enough.
  // The mode's banner replaces the `MAP - FOCUS` caption across the top of the pane, and this chip
  // is placed by measuring that caption -- so it would be laid out against a string that is not on
  // the screen, in the strip the banner has taken. And the rule the mode is built on is that the
  // map offers destinations and nothing else: a chip here would be a second thing to tap, whose
  // effect is to un-centre the very system the move is being chosen from.
  if (!m_mapView.AtAuthoredFraming() && !m_moveMode.has_value())
  {
    const float chipX =
      Frame::DIGEST_WIDTH + 12.0F + static_cast<float>(FontRenderer::MeasurePixels(FocusLine(m_state, m_focusedSystem))) + 10.0F;
    const float chipY = Frame::TOP_BAR_HEIGHT + 8.0F;
    const auto chipWidth = static_cast<float>(FontRenderer::MeasurePixels("RESET")) + 12.0F;
    // Drawn at the chip height the map's other chrome uses, hit at the floor around it (ADR-100):
    // it stands alone in the corner of a pane with nothing to overlap.
    constexpr float CHIP_HEIGHT = 22.0F;
    _shapes.StrokeRect(chipX, chipY, chipWidth, CHIP_HEIGHT, Ink::OUTLINE);
    _text.DrawText(static_cast<std::int32_t>(chipX) + 6, CenterTextY(chipY, CHIP_HEIGHT), "RESET", Ink::TEXT_MUTED);
    AddHit(chipX - (TOUCH_FLOOR - chipWidth) * 0.5F, chipY - (TOUCH_FLOOR - CHIP_HEIGHT) * 0.5F, std::max(chipWidth, TOUCH_FLOOR),
           TOUCH_FLOOR, Action::ResetCamera, 0);
  }

  for (const MapHit& hit : mapHits)
  {
    // **While a move is being chosen, the map offers destinations and nothing else** (ADR-114). The
    // discs, the badges and the markers register no hit at all in that frame, so a tap on one falls
    // through to `HandleTap`'s own end and leaves the mode.
    if (hit.moveTarget != EventRefs::NONE)
    {
      AddHit(hit.x, hit.y, hit.width, hit.height, Action::ChooseDestination, hit.moveTarget);
      continue;
    }
    // **A garrison badge opens the same sheet the disc under it opens** (ADR-079, ADR-112), which
    // answers ADR-079's open question: the badge followed the rail's rule and went focus-only at
    // the lock while the disc beside it opened a sheet, and they were two rules because they
    // opened two different things. They open one sheet now, so they behave alike -- at the lock it
    // stays open and goes inert like any other (ADR-065). The two targets remain two, because they
    // still name two things: the system, and the ships standing on it.
    if (hit.fleetsAt != EventRefs::NONE)
    {
      AddHit(hit.x, hit.y, hit.width, hit.height, Action::OpenFleetsAt, hit.fleetsAt);
      continue;
    }

    if (hit.system != EventRefs::NONE)
    {
      AddHit(hit.x, hit.y, hit.width, hit.height, Action::OpenSystem, hit.system);
      continue;
    }

    // A marker on a lane is either a move ordered this tick, drawn at progress zero until the lock
    // (ADR-055), or a fleet the server already has in transit. Only the first takes an order
    // (ADR-077); the second focuses where it is going, which is what a marker is asked about once
    // there is nothing to decide about it.
    const bool underWay = hit.fleet >= 0 && hit.fleet < static_cast<std::int32_t>(m_state.fleets.size()) &&
                          m_state.fleets[static_cast<std::size_t>(hit.fleet)].underWay;
    const std::int32_t destination = underWay ? m_state.fleets[static_cast<std::size_t>(hit.fleet)].to : hit.fleet;
    AddHit(hit.x, hit.y, hit.width, hit.height, underWay ? Action::FocusSystem : Action::BeginMove, destination);
  }
}

} // namespace Lockstep
