// MapRender.cpp -- the galaxy, drawn.
//
// Lifted out of `MainPage.cpp` whole (ADR-045). What it draws, how it projects and why it clips are
// unchanged; what changed is that they are no longer in the same file as three rails and a modal.

#include "pch.h"
#include "MapRender.h"

#include "DesignTokens.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace Lockstep
{

using Neuron::Color;
using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

namespace
{

constexpr Color MAP_TOP = {8, 10, 16, 255};
constexpr Color MAP_MIDDLE = {16, 22, 36, 255};
constexpr Color MAP_BOTTOM = {11, 14, 20, 255};
constexpr Color GRID_LINE = {94, 196, 255, 18};
constexpr Color HORIZON_GLOW = {94, 196, 255, 26};
constexpr Color HORIZON_GLOW_RIM = {94, 196, 255, 0};
constexpr Color STAR = {214, 220, 228, 220};
constexpr Color LANE_PLAIN = {214, 220, 228, 71};
/// The sealed region's reach on the plane, in design units. Shared with `MainPage`, which frames
/// the camera around everything including this.
constexpr float REGION_RADIUS = 62.0F;

constexpr std::int32_t LINE_HEIGHT = 12;

/// How far the sealed region's volume stands off the plane.
constexpr float REGION_VOLUME_HEIGHT = 26.0F;

constexpr float TWO_PI = 6.28318530717958647692F;
constexpr float GRID_MIN_DESIGN = -240.0F;
constexpr float GRID_MAX_DESIGN = 1040.0F;
constexpr float GRID_STEP = 80.0F;
constexpr std::uint32_t GRID_LINES_ACROSS = static_cast<std::uint32_t>((GRID_MAX_DESIGN - GRID_MIN_DESIGN) / GRID_STEP);
constexpr float NODE_RADIUS = 4.5F * 1.15F;
constexpr float CAPITAL_RADIUS = 6.0F * 1.15F;
constexpr float STEM_HEIGHT = 20.0F;
constexpr float CAPITAL_STEM_HEIGHT = 30.0F;
constexpr float SITE_PIN_HEIGHT = 14.0F;
constexpr float SHADOW_WIDE = 2.2F;
constexpr float SHADOW_TALL = 0.9F;
constexpr float HALO_SCALE = 2.4F;
constexpr float RING_SCALE = 2.2F;
constexpr float FLEET_HOVER = 14.0F;

// ---- A fleet's route (ADR-055) -----------------------------------------------------------------
//
// **Dots rather than dashes, and they travel.** A route has to be distinguishable at a glance from
// the two other lines that can run between the same two systems -- a trade lane, solid and blue,
// and a proposed one, dashed and blue -- and it carries its owner's colour like everything else a
// player owns, so colour alone cannot do it. What separates it is the SHAPE and the MOTION: a
// short dot on a long gap, walking toward the destination at a steady speed in pixels, which no
// static line on this map does.
constexpr float ROUTE_DOT = 2.5F;
constexpr float ROUTE_GAP = 7.5F;
constexpr float ROUTE_THICKNESS = 2.0F;
/// Pixels a dot travels each second. Slow enough to read as "under way" rather than as a barber's
/// pole, and fast enough that a glance of a second sees it move.
constexpr float ROUTE_SPEED_PIXELS_PER_SECOND = 18.0F;
/// The route is under the fleet that flies it, not beside it.
constexpr std::uint8_t ROUTE_ALPHA = 180;

void DrawGroundCircle(ShapeRenderer& _shapes, const MapFrame& _frame, float _designX, float _designY, float _radius, const Color& _fill,
                      const Color& _outline, bool _dashed, float _height = 0.0F)
{
  // A circle drawn ON the plane and projected, so the camera decides what shape it makes: a thin
  // sliver from a low angle, round from overhead, and nothing here has to know which.
  constexpr std::uint32_t SEGMENTS = 40;
  std::array<Neuron::OrbitCamera::ScreenPoint, SEGMENTS> rim = {};

  for (std::uint32_t segment = 0; segment < SEGMENTS; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(SEGMENTS);
    rim[segment] =
      _frame.view.Camera().Project(MapView::Above(_designX + _radius * std::cos(angle), _designY + _radius * std::sin(angle), _height));
    if (!rim[segment].visible)
    {
      return;
    }
  }

  if (_fill.alpha > 0)
  {
    // A fan from the centre. A circle projected from outside its own plane stays convex, so a fan
    // is enough and there is nothing to triangulate.
    const Neuron::OrbitCamera::ScreenPoint center = _frame.view.Camera().Project(MapView::Above(_designX, _designY, _height));
    if (center.visible)
    {
      for (std::uint32_t segment = 0; segment < SEGMENTS; ++segment)
      {
        const Neuron::OrbitCamera::ScreenPoint& a = rim[segment];
        const Neuron::OrbitCamera::ScreenPoint& b = rim[(segment + 1) % SEGMENTS];
        _shapes.FillTriangle(center.xPixels, center.yPixels, a.xPixels, a.yPixels, b.xPixels, b.yPixels, _fill);
      }
    }
  }

  if (_outline.alpha > 0)
  {
    for (std::uint32_t segment = 0; segment < SEGMENTS; ++segment)
    {
      if (_dashed && (segment % 2) == 1)
      {
        continue;
      }
      const Neuron::OrbitCamera::ScreenPoint& a = rim[segment];
      const Neuron::OrbitCamera::ScreenPoint& b = rim[(segment + 1) % SEGMENTS];
      _shapes.Line(a.xPixels, a.yPixels, b.xPixels, b.yPixels, _outline, 1.0F);
    }
  }
}

void DrawSystem(ShapeRenderer& _shapes, FontRenderer& _text, const MapFrame& _frame, std::vector<MapHit>& _hits, std::int32_t _index)
{
  const Neuron::OrbitCamera& camera = _frame.view.Camera();
  const SystemNode& node = _frame.state.graph.systems[static_cast<std::size_t>(_index)];

  const bool capital = HasFlag(node.flags, SystemFlags::Capital);
  const float worldRadius = capital ? CAPITAL_RADIUS : NODE_RADIUS;
  const float stemHeight = capital ? CAPITAL_STEM_HEIGHT : STEM_HEIGHT;

  const Neuron::OrbitCamera::ScreenPoint ground = camera.Project(MapView::Ground(node.positionX, node.positionY));
  const Neuron::OrbitCamera::ScreenPoint top = camera.Project(MapView::Above(node.positionX, node.positionY, stemHeight));
  if (!ground.visible || !top.visible)
  {
    return;
  }

  const Color owner = OwnerColor(node.owner, _frame.state.viewer);
  // Sized at the depth the NODE is at, not the ground point below it: a stem leans away from the
  // camera, so the two stop being the same distance once the view is steep.
  const float radius = worldRadius * camera.PixelsPerWorldUnitAt(top.depth);

  // The shadow is a ground circle, so it deforms with the camera like everything else on the
  // plane -- round from overhead, a sliver from low down.
  DrawGroundCircle(_shapes, _frame, node.positionX, node.positionY, worldRadius * SHADOW_WIDE, WithAlpha(owner, 56), {0, 0, 0, 0}, false);

  _shapes.Line(ground.xPixels, ground.yPixels, top.xPixels, top.yPixels, WithAlpha(owner, 140));

  if (capital)
  {
    _shapes.FillEllipse(top.xPixels, top.yPixels, radius * HALO_SCALE, radius * HALO_SCALE, WithAlpha(owner, 46));
  }
  if (HasFlag(node.flags, SystemFlags::Contested))
  {
    _shapes.StrokeEllipse(top.xPixels, top.yPixels, radius * RING_SCALE, radius * RING_SCALE, owner);
  }
  if (node.custodianSince != 0)
  {
    _shapes.DashedEllipse(top.xPixels, top.yPixels, radius * 2.0F, radius * 2.0F, owner, 1.0F, 2.0F, 3.0F);
  }
  if (_index == _frame.focusedSystem)
  {
    _shapes.StrokeEllipse(top.xPixels, top.yPixels, radius * 3.0F, radius * 3.0F, Ink::TEXT_PRIMARY);
  }

  _shapes.FillEllipse(top.xPixels, top.yPixels, radius, radius, owner);

  // Labels are 8px at every distance. The reference draws every map label at one size and the
  // game has one font at one size (ADR-014), so a far system's name is exactly as legible as a
  // near one's -- which on a map you read rather than admire is the right trade.
  std::string label = node.name;
  if (capital)
  {
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char _c) { return static_cast<char>(std::toupper(_c)); });
  }
  DrawCentered(_text, top.xPixels, static_cast<std::int32_t>(std::lround(top.yPixels - radius)) - 13, label, Ink::TEXT_PRIMARY);

  if (node.custodianSince != 0)
  {
    DrawCentered(_text, ground.xPixels, static_cast<std::int32_t>(std::lround(ground.yPixels)) + 8,
                 std::format("CUSTODIAN T{}", node.custodianSince), Ink::TEXT_MUTED);
  }
  if (node.capturedAt != 0)
  {
    DrawCentered(_text, ground.xPixels, static_cast<std::int32_t>(std::lround(ground.yPixels)) + 8,
                 std::format("CAPTURED T{}", node.capturedAt), Ink::RED);
  }

  _hits.push_back(MapHit{.x = top.xPixels - radius * 3.0F,
                         .y = top.yPixels - radius * 3.0F,
                         .width = radius * 6.0F,
                         .height = (ground.yPixels - top.yPixels) + radius * 6.0F,
                         .system = _index});
}

void DrawFleet(ShapeRenderer& _shapes, FontRenderer& _text, const MapFrame& _frame, std::vector<MapHit>& _hits, std::int32_t _index)
{
  const Neuron::OrbitCamera& camera = _frame.view.Camera();
  const Fleet& fleet = _frame.state.fleets[static_cast<std::size_t>(_index)];

  const SystemNode& from = _frame.state.graph.systems[static_cast<std::size_t>(fleet.from)];
  const SystemNode& to = _frame.state.graph.systems[static_cast<std::size_t>(fleet.to)];
  const float designX = from.positionX + (to.positionX - from.positionX) * fleet.progress;
  const float designY = from.positionY + (to.positionY - from.positionY) * fleet.progress;

  const Neuron::OrbitCamera::ScreenPoint foot = camera.Project(MapView::Ground(designX, designY));
  const Neuron::OrbitCamera::ScreenPoint head = camera.Project(MapView::Above(designX, designY, FLEET_HOVER));
  if (!foot.visible || !head.visible)
  {
    return;
  }

  const Color owner = OwnerColor(fleet.owner, _frame.state.viewer);
  _shapes.Line(foot.xPixels, foot.yPixels, head.xPixels, head.yPixels, WithAlpha(owner, 153));

  // The arrowhead points along the lane IN WORLD SPACE and is then projected, so it turns with the
  // camera and keeps meaning "that way" rather than "that way on the screen when the map happened
  // to be seen from the front".
  const Neuron::OrbitCamera::ScreenPoint ahead = camera.Project(
    MapView::Above(designX + (to.positionX - from.positionX) * 0.02F, designY + (to.positionY - from.positionY) * 0.02F, FLEET_HOVER));
  float dirX = 1.0F;
  float dirY = 0.0F;
  if (ahead.visible)
  {
    const float runX = ahead.xPixels - head.xPixels;
    const float runY = ahead.yPixels - head.yPixels;
    const float run = std::sqrt(runX * runX + runY * runY);
    if (run > 0.001F)
    {
      dirX = runX / run;
      dirY = runY / run;
    }
  }

  constexpr float ARROW = 6.0F;
  _shapes.FillTriangle(head.xPixels + dirX * ARROW, head.yPixels + dirY * ARROW, head.xPixels - dirX * ARROW - dirY * ARROW * 0.8F,
                       head.yPixels - dirY * ARROW + dirX * ARROW * 0.8F, head.xPixels - dirX * ARROW + dirY * ARROW * 0.8F,
                       head.yPixels - dirY * ARROW - dirX * ARROW * 0.8F, owner);

  // Two fleets converging on one system put their labels in the same place -- which is what is
  // happening at Kepler-Reach in the reference, and is the normal case rather than an edge one.
  // Yours goes above the arrowhead, where you are already looking; a rival's goes beside and below
  // it, so the two can never overlap.
  const std::string label = std::format("{} - ETA T{}", fleet.name, fleet.eta);
  const auto labelWidth = static_cast<float>(FontRenderer::MeasurePixels(label));
  const auto labelY = static_cast<std::int32_t>(std::lround(head.yPixels - 18.0F));
  const float paneX = Frame::DIGEST_WIDTH;
  const float paneWidth = Frame::SCREEN_WIDTH - Frame::DIGEST_WIDTH - Frame::ORDERS_WIDTH;

  if (fleet.owner == _frame.state.viewer)
  {
    const float clamped = std::clamp(head.xPixels, paneX + labelWidth * 0.5F + 4.0F, paneX + paneWidth - labelWidth * 0.5F - 4.0F);
    DrawCentered(_text, clamped, labelY, label, owner);
    _hits.push_back(MapHit{.x = head.xPixels - 14.0F, .y = head.yPixels - 22.0F, .width = 28.0F, .height = 36.0F, .fleet = _index});
  }
  else
  {
    const float placed = std::min(head.xPixels + 10.0F, paneX + paneWidth - labelWidth - 4.0F);
    _text.DrawText(static_cast<std::int32_t>(std::lround(placed)), labelY + LINE_HEIGHT, label, owner);
  }
}

} // namespace

std::vector<MapHit> DrawMap(ShapeRenderer& _shapes, FontRenderer& _text, const MapFrame& _frame)
{
  std::vector<MapHit> hits;

  const float paneX = Frame::DIGEST_WIDTH;
  const float paneWidth = Frame::SCREEN_WIDTH - Frame::DIGEST_WIDTH - Frame::ORDERS_WIDTH;
  const float paneHeight = Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT;
  _frame.view.SetViewport(paneX, Frame::TOP_BAR_HEIGHT, paneWidth, paneHeight);
  _frame.view.FrameContent(_frame.contentCenter, _frame.contentRadius, CAPITAL_STEM_HEIGHT + CAPITAL_RADIUS);
  const Neuron::OrbitCamera& camera = _frame.view.Camera();

  _shapes.FillVerticalGradient(paneX, Frame::TOP_BAR_HEIGHT, paneWidth, paneHeight, MAP_TOP, MAP_MIDDLE, 0.45F, MAP_BOTTOM);
  _text.SetClipRect(paneX, Frame::TOP_BAR_HEIGHT, paneWidth, paneHeight);

  // The sky goes through the same camera as everything else, because it is in the same world --
  // infinitely far away in it, which is a direction rather than a place (ADR-032). It is drawn
  // first and depth-tests against nothing, so the galaxy covers it.
  _frame.sky.Draw(_shapes, camera, STAR);

  const auto project = [&camera](const Neuron::OrbitCamera::WorldPoint& _world) { return camera.Project(_world); };
  const auto groundOf = [&](std::int32_t _system)
  {
    const SystemNode& node = _frame.state.graph.systems[static_cast<std::size_t>(_system)];
    return project(MapView::Ground(node.positionX, node.positionY));
  };

  // A segment between two projected points, drawn only when both ends are in front of the eye.
  // Clipping the one-end case properly would be the right answer for a camera that can be put
  // inside the galaxy; this one orbits outside it, so a lane with one end behind the eye is a
  // lane at the very edge of a steep view, and dropping it is not visible.
  const auto segment = [&_shapes](const Neuron::OrbitCamera::ScreenPoint& _a, const Neuron::OrbitCamera::ScreenPoint& _b,
                                  const Color& _color, float _thickness)
  {
    if (_a.visible && _b.visible)
    {
      _shapes.Line(_a.xPixels, _a.yPixels, _b.xPixels, _b.yPixels, _color, _thickness);
    }
  };

  // The ground grid, now genuinely on the ground: lines of constant x and constant z, projected.
  // It turns with the camera because it is part of the world, and that single change is most of
  // what makes the rotation read as a viewpoint moving rather than a picture being spun (ADR-017).
  for (std::uint32_t step = 0; step <= GRID_LINES_ACROSS; ++step)
  {
    const float at = GRID_MIN_DESIGN + static_cast<float>(step) * GRID_STEP;
    segment(project(MapView::Ground(at, GRID_MIN_DESIGN)), project(MapView::Ground(at, GRID_MAX_DESIGN)), GRID_LINE, 1.0F);
    segment(project(MapView::Ground(GRID_MIN_DESIGN, at)), project(MapView::Ground(GRID_MAX_DESIGN, at)), GRID_LINE, 1.0F);
  }

  // Lanes lie on the plane, under everything that stands on it.
  for (const Lane& lane : _frame.state.graph.lanes)
  {
    const Neuron::OrbitCamera::ScreenPoint a = groundOf(lane.a);
    const Neuron::OrbitCamera::ScreenPoint b = groundOf(lane.b);
    if (!a.visible || !b.visible)
    {
      continue;
    }

    switch (lane.kind)
    {
    case LaneKind::Trade:
      // A trade lane is public and meant to be read at a glance: it is the one consensual
      // mechanic in the game, and cancelling one is a tell (one-pager, decision 3).
      _shapes.Line(a.xPixels, a.yPixels, b.xPixels, b.yPixels, Ink::BLUE, 2.5F);
      break;
    case LaneKind::Proposed:
      _shapes.DashedLine(a.xPixels, a.yPixels, b.xPixels, b.yPixels, Ink::BLUE, 2.0F, 4.0F, 5.0F);
      break;
    case LaneKind::None:
    default:
      _shapes.Line(a.xPixels, a.yPixels, b.xPixels, b.yPixels, LANE_PLAIN, 1.2F);
      break;
    }

    DrawCentered(_text, (a.xPixels + b.xPixels) * 0.5F, static_cast<std::int32_t>(std::lround((a.yPixels + b.yPixels) * 0.5F)) - 4,
                 std::to_string(lane.cost), Ink::TEXT_DETAIL);
  }

  // ---- Routes ----------------------------------------------------------------------------------
  //
  // **Where every fleet under way is GOING, drawn from origin to destination** (ADR-055). On the
  // plane with the lanes and under everything that stands on it: a route is a fact about the
  // ground, and one drawn over the systems would hide the two it is about.
  //
  // Every route, not only the viewer's. A fleet in transit is public once departed -- commitment
  // is blind at the moment of choice and visible afterwards -- and the whole point of the rule is
  // that a rival's committed move can be read and answered.
  //
  // The line is drawn origin first, and a growing offset walks the pattern toward the SECOND
  // endpoint -- so the dots travel the way the fleet is going, which is the only direction that
  // means anything.
  for (const Fleet& fleet : _frame.state.fleets)
  {
    if (fleet.order != FleetStance::Move || fleet.from == fleet.to)
    {
      continue;
    }

    const Neuron::OrbitCamera::ScreenPoint origin = groundOf(fleet.from);
    const Neuron::OrbitCamera::ScreenPoint destination = groundOf(fleet.to);
    if (!origin.visible || !destination.visible)
    {
      continue;
    }

    _shapes.DashedLine(origin.xPixels, origin.yPixels, destination.xPixels, destination.yPixels,
                       WithAlpha(OwnerColor(fleet.owner, _frame.state.viewer), ROUTE_ALPHA), ROUTE_THICKNESS, ROUTE_DOT, ROUTE_GAP,
                       _frame.animationSeconds * ROUTE_SPEED_PIXELS_PER_SECOND);
  }

  // The sealed region: everyone can see it and count down to it, which is what makes it a race
  // rather than a reward (one-pager, "Pacing devices").
  //
  // It is a CIRCLE on the ground, emitted as a projected polygon rather than as a screen-space
  // ellipse. An ellipse was right when the viewing angle could not change; now the shape a ground
  // circle makes depends on where the camera is, and projecting it is how it comes out right at
  // every angle for free.
  if (_frame.state.region.anchor != EventRefs::NONE)
  {
    const SystemNode& anchor = _frame.state.graph.systems[static_cast<std::size_t>(_frame.state.region.anchor)];
    DrawGroundCircle(_shapes, _frame, anchor.positionX, anchor.positionY, REGION_RADIUS, WithAlpha(Ink::PURPLE, 20), Ink::PURPLE, true);
    // A second ring, lifted. The reference drew one to suggest a volume rather than a puddle, and
    // with a real camera it does the job properly: it is a circle at altitude, so the gap between
    // the two rings opens and closes as the camera tilts.
    DrawGroundCircle(_shapes, _frame, anchor.positionX, anchor.positionY, REGION_RADIUS, {0, 0, 0, 0}, WithAlpha(Ink::PURPLE, 89), true,
                     REGION_VOLUME_HEIGHT);

    for (const auto& [offsetX, offsetY] : _frame.state.region.siteOffsets)
    {
      const Neuron::OrbitCamera::ScreenPoint foot = project(MapView::Ground(anchor.positionX + offsetX, anchor.positionY + offsetY));
      const Neuron::OrbitCamera::ScreenPoint head =
        project(MapView::Above(anchor.positionX + offsetX, anchor.positionY + offsetY, SITE_PIN_HEIGHT));
      segment(foot, head, WithAlpha(Ink::PURPLE, 153), 1.0F);
      if (head.visible)
      {
        _shapes.FillEllipse(head.xPixels, head.yPixels, 2.5F, 2.5F, Ink::PURPLE);
      }
    }

    // Below the nearest point of the rim, not beside the centre: at a low camera angle the two
    // are almost the same place and the label lands inside the region.
    const Neuron::OrbitCamera::ScreenPoint label = project(MapView::Ground(anchor.positionX, anchor.positionY + REGION_RADIUS));
    if (label.visible)
    {
      DrawCentered(_text, label.xPixels, static_cast<std::int32_t>(std::lround(label.yPixels)) + 14,
                   std::format("SEALED - OPENS T{}", _frame.state.region.opensAt), Ink::PURPLE);
    }
  }

  // ---- SYSTEMS AND FLEETS, BACK TO FRONT ------------------------------------------------------
  //
  // THE SORT IS THE CAMERA'S DOING. With a fixed viewpoint the authored order was correct for
  // every frame and nothing had to decide it. An orbiting camera changes what is in front of what
  // as it moves, so the order has to be computed -- far first, because this renderer has no depth
  // buffer for interface geometry and painter's order is the whole of its occlusion model
  // (ADR-014, ADR-017).
  struct Drawable
  {
    float depth;
    std::int32_t index;
    bool isFleet;
  };

  std::vector<Drawable> drawables;
  drawables.reserve(_frame.state.graph.systems.size() + _frame.state.fleets.size());

  for (std::size_t index = 0; index < _frame.state.graph.systems.size(); ++index)
  {
    const SystemNode& node = _frame.state.graph.systems[index];
    if (HasFlag(node.flags, SystemFlags::RegionAnchor))
    {
      continue;
    }
    const Neuron::OrbitCamera::ScreenPoint ground = groundOf(static_cast<std::int32_t>(index));
    if (ground.visible)
    {
      drawables.push_back(Drawable{ground.depth, static_cast<std::int32_t>(index), false});
    }
  }

  for (std::size_t index = 0; index < _frame.state.fleets.size(); ++index)
  {
    const Fleet& fleet = _frame.state.fleets[index];
    if (fleet.order != FleetStance::Move || fleet.from == fleet.to)
    {
      continue;
    }
    const SystemNode& from = _frame.state.graph.systems[static_cast<std::size_t>(fleet.from)];
    const SystemNode& to = _frame.state.graph.systems[static_cast<std::size_t>(fleet.to)];
    const float designX = from.positionX + (to.positionX - from.positionX) * fleet.progress;
    const float designY = from.positionY + (to.positionY - from.positionY) * fleet.progress;
    const Neuron::OrbitCamera::ScreenPoint at = project(MapView::Ground(designX, designY));
    if (at.visible)
    {
      drawables.push_back(Drawable{at.depth, static_cast<std::int32_t>(index), true});
    }
  }

  std::sort(drawables.begin(), drawables.end(), [](const Drawable& _a, const Drawable& _b) { return _a.depth > _b.depth; });

  for (const Drawable& drawable : drawables)
  {
    if (drawable.isFleet)
    {
      DrawFleet(_shapes, _text, _frame, hits, drawable.index);
    }
    else
    {
      DrawSystem(_shapes, _text, _frame, hits, drawable.index);
    }
  }

  // `MAP - FOCUS: HALVORSEN` in the top-left corner (DESIGN-GUIDELINES "Map"). The map is no
  // longer captioned with a census -- that is on the top bar now -- and says instead what it is
  // currently pointed at, because the digest can point it somewhere.
  std::string focusLine = "MAP";
  if (_frame.focusedSystem != EventRefs::NONE && _frame.focusedSystem < static_cast<std::int32_t>(_frame.state.graph.systems.size()))
  {
    const SystemNode& focused = _frame.state.graph.systems[static_cast<std::size_t>(_frame.focusedSystem)];
    focusLine += focused.name.empty() ? " - FOCUS: THE FALLOW" : " - FOCUS: " + Uppercased(focused.name);
  }
  _text.DrawText(static_cast<std::int32_t>(paneX) + 12, static_cast<std::int32_t>(Frame::TOP_BAR_HEIGHT) + 12, focusLine, Ink::TEXT_DETAIL);

  // The legend earns its place: the owner colours are also the semantic colours, so a player who
  // learns this row can read every other coloured thing on the screen.
  struct LegendEntry
  {
    std::string label;
    Color color;
    bool isLane;
    bool dashed;
  };

  // The empires this player can actually see, not all twelve. A twelve-swatch legend would fill
  // the bar with colours for empires nobody has met, and the
  // entries that earn their place are the ones already on the map (ADR-027).
  const auto labelOf = [&_frame](OwnerId _player)
  {
    return _player >= 0 && _player < static_cast<OwnerId>(_frame.state.players.size())
             ? _frame.state.players[static_cast<std::size_t>(_player)].label
             : std::string("RIVAL");
  };

  std::vector<LegendEntry> legend;
  legend.push_back({labelOf(_frame.state.viewer), OwnerColor(_frame.state.viewer, _frame.state.viewer), false, false});

  std::vector<OwnerId> rivals;
  for (const SystemNode& node : _frame.state.graph.systems)
  {
    if (node.owner != NOBODY && node.owner != _frame.state.viewer && std::find(rivals.begin(), rivals.end(), node.owner) == rivals.end())
    {
      rivals.push_back(node.owner);
    }
  }
  std::sort(rivals.begin(), rivals.end());

  // Four rivals is what fits beside the two lane entries at 8px. Past that the map is its own
  // legend: every system carries a name, and tapping one says who holds it.
  constexpr std::size_t MOST_RIVALS_SHOWN = 4;
  for (std::size_t index = 0; index < rivals.size() && index < MOST_RIVALS_SHOWN; ++index)
  {
    legend.push_back({labelOf(rivals[index]), OwnerColor(rivals[index], _frame.state.viewer), false, false});
  }

  legend.push_back({"PROPOSED LANE", Ink::BLUE, true, true});
  legend.push_back({"TRADE LANE", Ink::BLUE, true, false});

  // Only when there is one on the map. A legend entry for a thing nobody can see is a colour to
  // learn for nothing, which is the rule the rival swatches above already follow (ADR-027).
  const bool anyMoving = std::any_of(_frame.state.fleets.begin(), _frame.state.fleets.end(),
                                     [](const Fleet& _fleet) { return _fleet.order == FleetStance::Move && _fleet.from != _fleet.to; });
  if (anyMoving)
  {
    legend.push_back({"FLEET UNDER WAY", Ink::TEXT_MUTED, true, true});
  }

  float legendX = paneX + 12.0F;
  const float legendY = Frame::SCREEN_HEIGHT - 20.0F;
  for (const LegendEntry& entry : legend)
  {
    if (entry.isLane)
    {
      if (entry.dashed)
      {
        _shapes.DashedLine(legendX, legendY + 4.0F, legendX + 14.0F, legendY + 4.0F, entry.color, 2.0F, 4.0F, 3.0F);
      }
      else
      {
        _shapes.Line(legendX, legendY + 4.0F, legendX + 14.0F, legendY + 4.0F, entry.color, 2.0F);
      }
      legendX += 19.0F;
    }
    else
    {
      _shapes.FillEllipse(legendX + 4.0F, legendY + 4.0F, 4.0F, 4.0F, entry.color);
      legendX += 13.0F;
    }

    _text.DrawText(static_cast<std::int32_t>(legendX), static_cast<std::int32_t>(legendY), entry.label, Ink::TEXT_DETAIL);
    legendX += static_cast<float>(FontRenderer::MeasurePixels(entry.label)) + 14.0F;
  }

  _text.ClearClipRect();

  return hits;
}

} // namespace Lockstep
