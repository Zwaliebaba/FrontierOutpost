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
#include <utility>

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

/// Baseline to baseline, from the font. See `MainPage::LINE_HEIGHT`.
constexpr std::int32_t LINE_HEIGHT = static_cast<std::int32_t>(Neuron::FontRenderer::LineHeightPixels());

/// How far the sealed region's volume stands off the plane.
constexpr float REGION_VOLUME_HEIGHT = 26.0F;

constexpr float TWO_PI = 6.28318530717958647692F;
constexpr float GRID_MIN_DESIGN = -240.0F;
constexpr float GRID_MAX_DESIGN = 1040.0F;
constexpr float GRID_STEP = 80.0F;
constexpr std::uint32_t GRID_LINES_ACROSS = static_cast<std::uint32_t>((GRID_MAX_DESIGN - GRID_MIN_DESIGN) / GRID_STEP);
constexpr float NODE_RADIUS = 4.5F * 1.15F;
constexpr float CAPITAL_RADIUS = 6.0F * 1.15F;
/// Where a station stands when its yield is unknown, and the least a capital ever stands (ADR-103).
constexpr float STEM_HEIGHT = 20.0F;
constexpr float CAPITAL_STEM_HEIGHT = 30.0F;
constexpr float SITE_PIN_HEIGHT = 14.0F;
constexpr float HALO_SCALE = 2.4F;
constexpr float RING_SCALE = 2.2F;
constexpr float FLEET_HOVER = 14.0F;

// ---- A station's height is its yield (ADR-103) -------------------------------------------------
//
// **The map's third axis carries data.** Design space is flat and height is the axis it did not
// have (ADR-017); until now a stem was one of two fixed numbers and said only "capital" or "not".
// Now a station stands as tall as the credits it pays a tick, its footprint on the plane reaches
// as far, and the number is written under its foot -- three readings of one fact, so a rich system
// is seen before it is read.

/// World units at nothing, and per credit a tick on top. Fourteen and three put the base yield
/// (two credits) at exactly `STEM_HEIGHT`, so a system that pays the base stands where every
/// system stood before the axis meant anything and a board the server has not priced looks the
/// same; a capital with a full mining station stands at sixty-two.
constexpr float STEM_BASE = 14.0F;
constexpr float STEM_PER_UNIT = 3.0F;
/// The dashed ring on the plane, in world units at nothing and per credit. One rather than three a
/// credit, because footprints are side by side where stems are not: a satellite is a short lane
/// from its capital, and two footprints that met would read as one territory.
constexpr float FOOTPRINT_BASE = 8.0F;
constexpr float FOOTPRINT_PER_UNIT = 1.0F;
constexpr float FOOTPRINT_DASH = 3.0F;
/// A rung every ten world units up the stem, so a height can be READ against a scale rather than
/// only compared with its neighbour's. Four pixels wide, one tall, centred on the stem.
constexpr float STEM_RUNG_SPACING = 10.0F;
constexpr float STEM_RUNG_WIDTH = 4.0F;
/// The contact shadow under a ball and the disc it stands on, as multiples of the ball's radius.
/// The shadow is black and wider than the ball; the disc is the owner's colour and narrower, so
/// the two read as a ball standing on a plate rather than as a puddle of the owner's colour.
constexpr float CONTACT_SHADOW_SCALE = 1.4F;
constexpr float OWNER_DISC_SCALE = 0.9F;

/// One light for the whole galaxy, in WORLD space: normalize(-0.45, 0.60, 0.65), from above, from
/// the -x side, and from +z -- toward the eye at the opening yaw, so the balls open lit. Fixed to
/// the world and not to the eye, which is the whole point: orbit the camera and the lit side of
/// every ball turns with the galaxy, and the map reads as a place seen from somewhere rather than
/// a picture with a highlight painted on (ADR-103).
constexpr Neuron::OrbitCamera::WorldPoint LIGHT_DIRECTION = {-0.87905F, 0.46538F, 0.10342F};

/// Where a station's shadow falls, and it is DELIBERATELY NOT THE KEY LIGHT (ADR-105).
///
/// normalize(-0.51, 0.86, 0.06): the same bearing across the plane as the key, and much steeper. An
/// honest shadow from a light raked this far is 1.9 times the stem's height -- 38 world units for a
/// base station and 118 for a rich capital, on a map whose systems sit 40 to 80 units apart -- so
/// every tall station would throw its shadow across its neighbour's footprint. This one lands at
/// 0.60 of the height: 12 units and 37, each beside its own foot.
///
/// **It is a lie and it is the lie every stylised renderer tells.** The mismatch between where the
/// light visibly comes from and where the shadow falls is not readable at this scale, and what it
/// buys is a shadow that says how tall a station is instead of which neighbour it is nearest.
constexpr Neuron::OrbitCamera::WorldPoint SHADOW_DIRECTION = {-0.50916F, 0.85858F, 0.05990F};

/// Half the width of a stem's column, in world units. About two canvas pixels at the opening
/// framing and more as the player zooms in -- wide enough to read as a solid with two faces, which
/// a 1px line never could (ADR-105).
constexpr float STEM_HALF_WIDTH = 1.2F;

/// A fleet's marker, in world units (ADR-106). It is a DART -- an octahedron stretched along the
/// lane -- rather than the flat triangle it replaces, so it is depth-tested against the stations (a
/// fleet behind a system used to draw in front of it) and it catches the same light. Longer than it
/// is wide, because the one thing the triangle carried was which way the fleet is going.
constexpr float FLEET_HALF_LENGTH = 5.0F;
constexpr float FLEET_HALF_WIDTH = 2.2F;
constexpr float FLEET_HALF_HEIGHT = 2.2F;

/// The garrison badge beside a system's name (ADR-079). 16 is the `LOCKED` chip's height, which is
/// what a chip is on this screen; there is no rounded-rectangle primitive and every other chip here
/// is square, so this one is too.
constexpr float BADGE_HEIGHT = 16.0F;
/// What a finger gets, around what the eye gets (ADR-100). `MainPage::TOUCH_FLOOR` is the number;
/// it is restated here because `MapRender` does not include `MainPage.h` -- the map knows nothing
/// about the page that draws it (ADR-045) -- and `TouchTargetTests` is what holds the two together.
constexpr float BADGE_TOUCH_FLOOR = 44.0F;
constexpr float BADGE_PADDING = 4.0F;
/// A rival's badge is a wash rather than a fill: their strength is a fact to read, and only the
/// viewer's own badge is a thing to tap.
constexpr std::uint8_t BADGE_RIVAL_ALPHA = 89;

/// A capture that is neither the viewer's gain nor their loss: the new owner's colour, softened,
/// because it is news about somebody else's board (ADR-088). 0.7, which UI-01 1.4 asked for.
constexpr std::uint8_t CAPTURE_GAIN_ALPHA = 179;

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

/// How far, in screen pixels, a fleet marker is kept clear of the systems at either end of its
/// lane (ADR-059).
///
/// **A fleet is drawn where its remaining ticks put it, and at the ends of a lane that is on top of
/// a system.** A move ordered and not yet locked sits at zero, which is exactly on the node it is
/// leaving; the first tick of a four-tick crossing is a quarter along, which on a short lane is
/// still inside that node's disc.
///
/// **Measured against the LABEL, not the marker**, because the label is the wide part: `FLT 1 - ETA
/// T6` is about fourteen glyphs, drawn centred on the marker, so half of it is ~56px either side --
/// and a capital's halo adds another twelve. Clearing only the arrowhead moved the triangle off the
/// node and left the text running through it.
///
/// A lane with no room for that at both ends puts the marker in the MIDDLE, which is the honest
/// answer when there is no room to say anything more precise, and is what a short lane will
/// usually get.
constexpr float FLEET_END_CLEARANCE = 68.0F;

// ---- The move being chosen on the map (ADR-113) ------------------------------------------------

/// A lit system's ring, in SCREEN pixels: it says *this is a target* rather than anything about how
/// big the system is, so it does not grow with the camera the way a halo does.
constexpr float MOVE_RING_RADIUS = 16.0F;
/// The ring a lit system wears once it is the chosen one.
constexpr float MOVE_SELECTED_RADIUS = 22.0F;
/// The pulse: a ring at this alpha, breathing to full and back on this period.
constexpr std::uint8_t MOVE_RING_ALPHA = 128;
constexpr float MOVE_PULSE_SECONDS = 1.6F;
/// How fast the dashes march toward the destination. Slower than a fleet's route (ADR-055), because
/// this is a lane being OFFERED rather than one being travelled.
constexpr float MOVE_DASH_PIXELS_PER_SECOND = 10.0F;
constexpr float MOVE_DASH = 4.0F;
constexpr float MOVE_GAP = 5.0F;
/// What every lane the move is not about drops to, so the lit ones are the only blue on the plane.
constexpr Color LANE_UNLIT = {214, 220, 228, 30};
/// The chip under a lit system's name: `1 TICK - T1`.
constexpr float MOVE_CHIP_HEIGHT = 16.0F;
constexpr float MOVE_CHIP_PADDING = 5.0F;
/// The outline the origin's garrison badge wears, at this offset, so the fleet being moved is
/// findable on a board where several systems are lit.
constexpr float MOVE_ORIGIN_OUTLINE = 2.0F;
constexpr float MOVE_ORIGIN_OFFSET = 1.0F;
/// The lowest the pulse falls, which is also where it sits at phase zero.
constexpr float MOVE_PULSE_FLOOR = 0.55F;

/// The move target for one system, or nothing when the map is not offering it (ADR-113).
[[nodiscard]] const MoveTarget* TargetFor(const MapFrame& _frame, std::int32_t _system) noexcept
{
  for (const MoveTarget& target : _frame.moveTargets)
  {
    if (target.system == _system)
    {
      return &target;
    }
  }
  return nullptr;
}

/// How strongly a lit system's ring is drawn this frame: 0.55 to 1 and back, and **0.55 at phase
/// zero**, so a capture taken with the clock stopped is always the same picture (ADR-113).
[[nodiscard]] float MovePulse(float _seconds) noexcept
{
  const float phase = std::fmod(_seconds, MOVE_PULSE_SECONDS) / MOVE_PULSE_SECONDS;
  const float swell = 0.5F - 0.5F * std::cos(phase * 2.0F * 3.14159265F);
  return MOVE_PULSE_FLOOR + (1.0F - MOVE_PULSE_FLOOR) * swell;
}

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

/// A dashed circle on the plane, its dash walked in SCREEN pixels around the projected rim, so two
/// footprints of different sizes read as the same material (`ShapeRenderer::DashedLine`'s reason).
/// The sealed region's ring keeps `DrawGroundCircle`'s dash of every other segment; this is a
/// different thing, and it is the station's (ADR-103).
void DrawGroundDashedRing(ShapeRenderer& _shapes, const MapFrame& _frame, float _designX, float _designY, float _radius,
                          const Color& _color, float _dashPixels)
{
  constexpr std::uint32_t SEGMENTS = 40;
  std::array<Neuron::OrbitCamera::ScreenPoint, SEGMENTS> rim = {};
  for (std::uint32_t segment = 0; segment < SEGMENTS; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(SEGMENTS);
    rim[segment] =
      _frame.view.Camera().Project(MapView::Ground(_designX + _radius * std::cos(angle), _designY + _radius * std::sin(angle)));
    if (!rim[segment].visible)
    {
      return;
    }
  }

  // Each chord continues the pattern from where the last one left it: an offset of minus the
  // distance walked so far is what puts the next dash boundary where the previous chord's
  // arithmetic says it falls, so the ring is one pattern and not forty.
  float walked = 0.0F;
  for (std::uint32_t segment = 0; segment < SEGMENTS; ++segment)
  {
    const Neuron::OrbitCamera::ScreenPoint& a = rim[segment];
    const Neuron::OrbitCamera::ScreenPoint& b = rim[(segment + 1) % SEGMENTS];
    _shapes.DashedLine(a.xPixels, a.yPixels, b.xPixels, b.yPixels, _color, 1.0F, _dashPixels, _dashPixels, -walked);
    const float runX = b.xPixels - a.xPixels;
    const float runY = b.yPixels - a.yPixels;
    walked += std::sqrt(runX * runX + runY * runY);
  }
}

/// Where the labels already drawn on this frame are, and where the lanes run, so the next one can
/// be put somewhere the reader can actually read it (ADR-090).
///
/// **One greedy pass, upward, and it gives up rather than searching.** The map is redrawn from
/// scratch every frame and must lay out identically twice for the same state -- a screenshot test
/// and an idle redraw both depend on it (ADR-047) -- so this is deterministic by construction: the
/// same depth-sorted order, the same fixed steps, no iteration to a fixed point. A label that
/// cannot be cleared in three steps is drawn where it was, because a label 36 pixels from the thing
/// it names has stopped being that thing's label.
struct LabelField
{
  struct Box
  {
    float left;
    float top;
    float right;
    float bottom;
  };

  struct Segment
  {
    float ax;
    float ay;
    float bx;
    float by;
  };

  std::vector<Box> placed;
  std::vector<Segment> lanes;

  [[nodiscard]] static bool Overlaps(const Box& _a, const Box& _b) noexcept
  {
    return _a.left < _b.right && _b.left < _a.right && _a.top < _b.bottom && _b.top < _a.bottom;
  }

  /// Whether a segment crosses a box. The four edges, plus the case of a segment wholly inside it.
  [[nodiscard]] static bool Crosses(const Segment& _segment, const Box& _box) noexcept
  {
    const bool insideA = _segment.ax >= _box.left && _segment.ax <= _box.right && _segment.ay >= _box.top && _segment.ay <= _box.bottom;
    if (insideA)
    {
      return true;
    }

    // A slab clip: the parametric range the segment is inside the box on each axis, intersected.
    float enter = 0.0F;
    float leave = 1.0F;
    const float runX = _segment.bx - _segment.ax;
    const float runY = _segment.by - _segment.ay;

    const auto slab = [&enter, &leave](float _run, float _from, float _low, float _high)
    {
      if (std::abs(_run) < 0.0001F)
      {
        return _from >= _low && _from <= _high;
      }
      // NOT `near` and `far`: both are empty macros in `minwindef.h`, which `NeuronCore.h` does not
      // suppress, and a local called either one vanishes into a syntax error.
      float entering = (_low - _from) / _run;
      float leaving = (_high - _from) / _run;
      if (entering > leaving)
      {
        std::swap(entering, leaving);
      }
      enter = std::max(enter, entering);
      leave = std::min(leave, leaving);
      return enter <= leave;
    };

    return slab(runX, _segment.ax, _box.left, _box.right) && slab(runY, _segment.ay, _box.top, _box.bottom) && enter <= leave;
  }

  /// The baseline this label should use: the one it asked for, or up to three steps above it.
  [[nodiscard]] std::int32_t Place(float _centerX, std::int32_t _baselineY, std::uint32_t _widthPixels)
  {
    /// A line box, so a nudged label clears the thing it collided with by a whole line rather than
    /// by a gap that still reads as touching.
    constexpr float STEP = 12.0F;
    constexpr std::int32_t MOST_STEPS = 3;

    const auto boxAt = [&](std::int32_t _at)
    {
      const auto top = static_cast<float>(_at);
      return Box{_centerX - static_cast<float>(_widthPixels) * 0.5F, top, _centerX + static_cast<float>(_widthPixels) * 0.5F,
                 top + static_cast<float>(FontRenderer::GlyphHeightPixels())};
    };

    std::int32_t at = _baselineY;
    for (std::int32_t step = 0; step <= MOST_STEPS; ++step)
    {
      const Box box = boxAt(at);
      const bool clear = std::none_of(placed.begin(), placed.end(), [&box](const Box& _other) { return Overlaps(box, _other); }) &&
                         std::none_of(lanes.begin(), lanes.end(), [&box](const Segment& _lane) { return Crosses(_lane, box); });
      if (clear || step == MOST_STEPS)
      {
        placed.push_back(box);
        return at;
      }
      at -= static_cast<std::int32_t>(STEP);
    }
    return at;
  }

  /// Claims a box at exactly `_baselineY` when nothing already placed overlaps it, and says whether
  /// it did. For ink that is optional -- the yield under a station (ADR-103) -- which is better left
  /// out than nudged to where it no longer belongs to its station. Lanes are not consulted: a
  /// station's foot is where its lanes meet, and a two-glyph number a lane runs under is still a
  /// number.
  [[nodiscard]] bool TryPlace(float _centerX, std::int32_t _baselineY, std::uint32_t _widthPixels)
  {
    const auto top = static_cast<float>(_baselineY);
    const Box box{_centerX - static_cast<float>(_widthPixels) * 0.5F, top, _centerX + static_cast<float>(_widthPixels) * 0.5F,
                  top + static_cast<float>(FontRenderer::GlyphHeightPixels())};
    if (std::any_of(placed.begin(), placed.end(), [&box](const Box& _other) { return Overlaps(box, _other); }))
    {
      return false;
    }
    placed.push_back(box);
    return true;
  }
};

/// The fleets STANDING at one system, gathered per owner. What a garrison badge says (ADR-079).
///
/// **Per owner rather than per fleet**, because a system holding three of your fleets is one
/// strength to read and one thing to tap, and the number a player weighs a lane by is the total.
/// Which fleets made it up is the fleet-list sheet's business, so the count and the first index
/// travel with it: one fleet opens its picker, several open the sheet that picks between them.
struct Garrison
{
  OwnerId owner = NOBODY;
  std::uint32_t ships = 0;
  std::int32_t fleets = 0;
  std::int32_t first = EventRefs::NONE;
};

/// Every owner with fleets standing at `_system`, the viewer first and the rest by owner id.
///
/// The viewer first because their own badge is the one they look for, and a stable order after that
/// because two frames of the same state must lay out the same way (`MapFrame::animationSeconds` is
/// the only thing on this map allowed to differ between them).
///
/// **This reveals nothing the client was not already told.** It reads `MatchState::fleets`, which is
/// what the snapshot sent through the fog -- a rival fleet the viewer cannot see is not in that list
/// and cannot be badged (ADR-022).
[[nodiscard]] std::vector<Garrison> GarrisonsAt(const MatchState& _state, std::int32_t _system)
{
  std::vector<Garrison> garrisons;
  for (std::size_t index = 0; index < _state.fleets.size(); ++index)
  {
    const Fleet& fleet = _state.fleets[index];
    if (fleet.OnALane() || fleet.to != _system || fleet.owner == NOBODY)
    {
      continue;
    }

    const auto found =
      std::find_if(garrisons.begin(), garrisons.end(), [&fleet](const Garrison& _garrison) { return _garrison.owner == fleet.owner; });
    if (found == garrisons.end())
    {
      garrisons.push_back(Garrison{.owner = fleet.owner, .ships = fleet.ships, .fleets = 1, .first = static_cast<std::int32_t>(index)});
      continue;
    }
    found->ships += fleet.ships;
    ++found->fleets;
  }

  const OwnerId viewer = _state.viewer;
  std::sort(garrisons.begin(), garrisons.end(),
            [viewer](const Garrison& _a, const Garrison& _b)
            {
              if ((_a.owner == viewer) != (_b.owner == viewer))
              {
                return _a.owner == viewer;
              }
              return _a.owner < _b.owner;
            });
  return garrisons;
}

/// Where a station stands on the screen this frame, from its node: what both halves of drawing it
/// need, computed the same way twice so the ground half and the overlay half cannot disagree.
struct Station
{
  bool visible = false;
  bool capital = false;
  float worldRadius = 0.0F;
  float stemHeight = 0.0F;
  Neuron::OrbitCamera::ScreenPoint ground = {};
  Neuron::OrbitCamera::ScreenPoint top = {};
  /// The ball's radius in pixels, sized at the depth the BALL is at rather than the ground point
  /// below it: a stem leans away from the camera, so the two stop being the same distance once the
  /// view is steep.
  float radiusPixels = 0.0F;
};

[[nodiscard]] Station PlaceStation(const MapFrame& _frame, const SystemNode& _node)
{
  const Neuron::OrbitCamera& camera = _frame.view.Camera();

  Station station;
  station.capital = HasFlag(_node.flags, SystemFlags::Capital);
  station.worldRadius = station.capital ? CAPITAL_RADIUS : NODE_RADIUS;
  station.stemHeight = StemHeightFor(_node.production, station.capital);
  station.ground = camera.Project(MapView::Ground(_node.positionX, _node.positionY));
  station.top = camera.Project(MapView::Above(_node.positionX, _node.positionY, station.stemHeight));
  station.visible = station.ground.visible && station.top.visible;
  station.radiusPixels = station.worldRadius * camera.PixelsPerWorldUnitAt(station.top.depth);
  return station;
}

/// The lit tone of a station's ball: its owner's colour, made opaque against the map's ground.
///
/// The mesh pass does not blend (ADR-014 gives blending to the interface passes and nothing else),
/// so a colour that carries an alpha -- unheld space is the neutral grey at 115 -- would draw at
/// full strength as a solid. Compositing it once, here, against the ground the ball stands over is
/// what keeps an unclaimed ball as muted as an unclaimed disc was.
[[nodiscard]] Color BallTone(const Color& _owner)
{
  return WithAlpha(Neuron::Mix(Ink::APP_BACKGROUND, _owner, static_cast<float>(_owner.alpha) / 255.0F), Neuron::OPAQUE_ALPHA);
}

/// The half of a station that lies UNDER its ball: shadow, disc, footprint, stem and halo into the
/// shape recorder, and the ball itself into the mesh recorder (ADR-103). Back to front, in the
/// order the mockup lists them.
void DrawStationGround(ShapeRenderer& _shapes, Neuron::MeshRenderer& _meshes, const MapFrame& _frame, std::int32_t _index)
{
  const SystemNode& node = _frame.state.graph.systems[static_cast<std::size_t>(_index)];
  const Station station = PlaceStation(_frame, node);
  if (!station.visible)
  {
    return;
  }

  const Color owner = OwnerColor(node.owner, _frame.state.viewer);
  constexpr Color NO_FILL = {0, 0, 0, 0};

  // ---- The cast shadow (ADR-105) ---------------------------------------------------------------
  //
  // **Offset along the light rather than centred under the foot**, which is what says the ball is
  // floating at a height rather than sitting at a point. It is the strongest grounding cue there
  // is, and it only became worth drawing when ADR-104 raked the key light: under a light sitting
  // behind the eye the offset was foreshortened to nothing.
  const float shadowReach = station.stemHeight / SHADOW_DIRECTION.y;
  DrawGroundCircle(_shapes, _frame, node.positionX - SHADOW_DIRECTION.x * shadowReach, node.positionY - SHADOW_DIRECTION.z * shadowReach,
                   station.worldRadius * CONTACT_SHADOW_SCALE, WithAlpha(Neuron::BLACK, Ink::STATION_SHADOW_ALPHA), NO_FILL, false);

  // The ground circles deform with the camera like everything else on the plane -- round from
  // overhead, a sliver from low down. The owner on the plate at the foot, and the yield as a
  // dashed reach around it.
  DrawGroundCircle(_shapes, _frame, node.positionX, node.positionY, station.worldRadius * OWNER_DISC_SCALE,
                   WithAlpha(owner, Ink::STATION_DISC_ALPHA), NO_FILL, false);
  DrawGroundDashedRing(_shapes, _frame, node.positionX, node.positionY, FootprintRadiusFor(node.production),
                       WithAlpha(owner, Ink::STATION_FOOTPRINT_ALPHA), FOOTPRINT_DASH);

  if (station.capital)
  {
    _shapes.FillEllipse(station.top.xPixels, station.top.yPixels, station.radiusPixels * HALO_SCALE, station.radiusPixels * HALO_SCALE,
                        WithAlpha(owner, 46));
  }

  // ---- The solid, into the mesh recorder -------------------------------------------------------
  //
  // **The stem is a column and not a line** (ADR-105). A 1px line is the same line at every depth
  // and under every light; a column has a face toward the light and a face away from it, converges
  // with distance, and is depth-tested against the balls like everything else standing up. Its
  // tones are the ball's, so a station is one material.
  //
  // The ball sits at the top of it. Ownership stays colour-only -- the shape is the same for
  // everybody.
  const Color ballTone = BallTone(owner);
  _meshes.Column(MapView::Ground(node.positionX, node.positionY), station.stemHeight, STEM_HALF_WIDTH, Ink::FlatRampFor(ballTone));
  _meshes.Sphere(MapView::Above(node.positionX, node.positionY, station.stemHeight), station.worldRadius, Ink::RampFor(ballTone),
                 Neuron::MeshRenderer::SegmentsForRadius(station.radiusPixels));
}

/// The half of a station that sits OVER its ball: the rings, the name, what is written under its
/// foot, its hit, and the garrison badges. Recorded after the layer boundary, so it is drawn after
/// the mesh pass.
void DrawStationOverlay(ShapeRenderer& _shapes, FontRenderer& _text, const MapFrame& _frame, std::vector<MapHit>& _hits,
                        LabelField& _labels, std::int32_t _index)
{
  const SystemNode& node = _frame.state.graph.systems[static_cast<std::size_t>(_index)];
  const Station station = PlaceStation(_frame, node);
  if (!station.visible)
  {
    return;
  }

  const Neuron::OrbitCamera::ScreenPoint& ground = station.ground;
  const Neuron::OrbitCamera::ScreenPoint& top = station.top;
  const float radius = station.radiusPixels;
  const bool capital = station.capital;
  const Color owner = OwnerColor(node.owner, _frame.state.viewer);

  // The rungs, a mark every ten world units so a height is a READING and not only a comparison with
  // the neighbour. They are over the column rather than under it: the stem is a solid now, and a
  // rung recorded before the mesh pass would be painted over by the very column it measures.
  // Stopping a ball's radius short of the top, because a rung drawn where the ball is is a rung
  // drawn ON the ball -- measured as a four-pixel bite out of Torvald's silhouette on the first
  // build of the column.
  const float highestRung = station.stemHeight - station.worldRadius;
  for (std::uint32_t step = 1; static_cast<float>(step) * STEM_RUNG_SPACING < highestRung; ++step)
  {
    const float height = static_cast<float>(step) * STEM_RUNG_SPACING;
    const Neuron::OrbitCamera::ScreenPoint rung = _frame.view.Camera().Project(MapView::Above(node.positionX, node.positionY, height));
    if (rung.visible)
    {
      _shapes.FillRect(std::round(rung.xPixels) - STEM_RUNG_WIDTH * 0.5F, std::round(rung.yPixels), STEM_RUNG_WIDTH, 1.0F,
                       WithAlpha(owner, Ink::STATION_STEM_ALPHA));
    }
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

  // **A lit system, while a move is being chosen on this map** (ADR-113). The ring is in screen
  // pixels rather than multiples of the ball, because it says *this is a target* and a target is
  // the same size wherever the camera has put the system. The chosen one stops breathing and
  // becomes a solid ring with a wash inside it -- the committed treatment every other surface uses
  // for *this is yours, it is queued* (ADR-110).
  const MoveTarget* offered = _frame.moveOrigin == EventRefs::NONE ? nullptr : TargetFor(_frame, _index);
  if (offered != nullptr)
  {
    if (_index == _frame.moveSelected)
    {
      _shapes.FillEllipse(top.xPixels, top.yPixels, MOVE_SELECTED_RADIUS, MOVE_SELECTED_RADIUS, WithAlpha(Ink::BLUE, 30));
      _shapes.StrokeEllipse(top.xPixels, top.yPixels, MOVE_SELECTED_RADIUS, MOVE_SELECTED_RADIUS, Ink::TEXT_PRIMARY);
    }
    else
    {
      const auto alpha = static_cast<std::uint8_t>(std::lround(static_cast<float>(MOVE_RING_ALPHA) * MovePulse(_frame.animationSeconds)));
      _shapes.StrokeEllipse(top.xPixels, top.yPixels, MOVE_RING_RADIUS, MOVE_RING_RADIUS, WithAlpha(Ink::BLUE, alpha));
    }
  }

  // Labels are 8px at every distance. The reference draws every map label at one size and the
  // game has one font at one size (ADR-014), so a far system's name is exactly as legible as a
  // near one's -- which on a map you read rather than admire is the right trade.
  std::string label = node.name;
  if (capital)
  {
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char _c) { return static_cast<char>(std::toupper(_c)); });
  }
  const std::int32_t labelY =
    _labels.Place(top.xPixels, static_cast<std::int32_t>(std::lround(top.yPixels - radius)) - 13, FontRenderer::MeasurePixels(label));
  DrawCentered(_text, top.xPixels, labelY, label, Ink::TEXT_PRIMARY);

  // The chip under the name, placed by the field the labels are placed by so it keeps out of the
  // way of the next system's (ADR-090). It carries what the strip's row carries -- how long, and the
  // tick it lands on -- because a player choosing on the map should not have to read the list to
  // find out.
  if (offered != nullptr)
  {
    const std::string chip =
      std::format("{} · T{}", offered->ticks == 1 ? std::string{"1 TICK"} : std::format("{} TICKS", offered->ticks), offered->arrivesAt);
    const auto chipWidth = static_cast<float>(FontRenderer::MeasurePixels(chip)) + 2.0F * MOVE_CHIP_PADDING;
    const std::int32_t chipTextY = labelY + LINE_HEIGHT;
    const float chipX = top.xPixels - chipWidth * 0.5F;
    const float chipY = BandTopForText(chipTextY, MOVE_CHIP_HEIGHT);
    const bool chosen = _index == _frame.moveSelected;

    _shapes.FillRect(chipX, chipY, chipWidth, MOVE_CHIP_HEIGHT, chosen ? Ink::BLUE : Ink::APP_BACKGROUND);
    if (!chosen)
    {
      _shapes.StrokeRect(chipX, chipY, chipWidth, MOVE_CHIP_HEIGHT, Ink::BLUE);
    }
    DrawCentered(_text, top.xPixels, chipTextY, chip, chosen ? Ink::APP_BACKGROUND : Ink::BLUE);
    _labels.placed.push_back(LabelField::Box{chipX, chipY, chipX + chipWidth, chipY + MOVE_CHIP_HEIGHT});

    // **Drawn at 16 and hit at the floor**, which is ADR-100's isolated-chip rule: the chip stays
    // the size the map has room for and the rectangle around it is what a finger aims at.
    const Frame::Box target = Frame::GrownToFloor(chipX, chipY, chipWidth, MOVE_CHIP_HEIGHT);
    _hits.push_back(MapHit{.x = target.x, .y = target.y, .width = target.width, .height = target.height, .moveTarget = _index});
  }

  const std::int32_t underFoot = static_cast<std::int32_t>(std::lround(ground.yPixels)) + 8;
  bool footTaken = false;
  if (node.custodianSince != 0)
  {
    DrawCentered(_text, ground.xPixels, underFoot, std::format("CUSTODIAN T{}", node.custodianSince), Ink::TEXT_MUTED);
    footTaken = true;
  }
  // **A capture is news for three ticks and then it is the map** (ADR-082). Six standing labels on
  // a board a player is winning is six things to read past on every tick, and none of them changed
  // this tick or the last two.
  //
  // **Red is what YOU lost, and the snapshot now says whose loss it was** (ADR-082, ADR-088). It
  // was drawn under every captured system including the ones the viewer took, so a winning board
  // read as a rout; then it could tell a gain from a loss but not a loss from a rival's loss.
  // `capturedFrom` is the answer to the second, so the three cases are three inks.
  if (CaptureIsNews(node.capturedAt, _frame.state.match.tick))
  {
    const bool lostByYou = node.capturedFrom == _frame.state.viewer;
    const Color ink = lostByYou ? Ink::RED : WithAlpha(owner, CAPTURE_GAIN_ALPHA);
    DrawCentered(_text, ground.xPixels, underFoot, std::format("CAPTURED T{}", node.capturedAt),
                 node.owner == _frame.state.viewer ? owner : ink);
    footTaken = true;
  }

  // **The yield, written where the stem meets the ground** (ADR-103): the number the height is a
  // picture of, in the owner's colour, one line lower when the foot already carries news. Left out
  // rather than nudged when a label is already there, because a number that has moved off its
  // station is a number about some other station.
  if (node.production != 0)
  {
    const std::string yield = std::format("+{}", node.production);
    const std::int32_t yieldY = footTaken ? underFoot + LINE_HEIGHT : underFoot;
    if (_labels.TryPlace(ground.xPixels, yieldY, FontRenderer::MeasurePixels(yield)))
    {
      DrawCentered(_text, ground.xPixels, yieldY, yield, WithAlpha(owner, Ink::STATION_YIELD_ALPHA));
    }
  }

  // **While a move is being chosen, a system is a destination or it is nothing at all** (ADR-113).
  // An unreachable one and a rival's are still drawn at their own ink -- the mode hides nothing --
  // and neither of them is a target, so a tap on one falls through to the map and leaves the mode,
  // which is what tapping the board means while a question is open.
  const bool moving = _frame.moveOrigin != EventRefs::NONE;
  if (!moving || offered != nullptr)
  {
    _hits.push_back(MapHit{.x = top.xPixels - radius * 3.0F,
                           .y = top.yPixels - radius * 3.0F,
                           .width = radius * 6.0F,
                           .height = (ground.yPixels - top.yPixels) + radius * 6.0F,
                           .system = moving ? EventRefs::NONE : _index,
                           .moveTarget = moving ? _index : EventRefs::NONE});
  }

  // ---- What is standing here (ADR-079) ----------------------------------------------------------
  //
  // **A fleet that is not on a lane was drawn by nothing at all until this badge.** The map's
  // drawables are systems and fleets in transit, so a board where the player holds ten fleets drew
  // none of them and the locks rail carried the whole of that answer.
  //
  // Pushed AFTER the system's own hit, because `MainPage` tests hits in reverse: the badge sits
  // inside the disc's generous rectangle and has to win it. The disc is the system and the badge is
  // the fleets, which is the whole reason they are two targets and not one.
  // **The badge rides on the label's baseline, including when the label was nudged** (ADR-090). The
  // two are read as one thing -- a name and what is standing at it -- so a label that moved up and a
  // badge that did not would come apart exactly on the crowded boards that made it move.
  float badgeX = std::max(top.xPixels + radius + 4.0F, top.xPixels + static_cast<float>(FontRenderer::MeasurePixels(label)) * 0.5F + 4.0F);

  for (const Garrison& garrison : GarrisonsAt(_frame.state, _index))
  {
    const std::string ships = std::to_string(garrison.ships);
    const float badgeWidth = static_cast<float>(FontRenderer::MeasurePixels(ships)) + BADGE_PADDING * 2.0F;
    const float badgeTop = BandTopForText(labelY, BADGE_HEIGHT);
    const Color color = OwnerColor(garrison.owner, _frame.state.viewer);
    const bool yours = garrison.owner == _frame.state.viewer;

    // Yours is filled and reads as a control, because it is one; a rival's is a wash of their colour
    // and reads as a fact, because that is all it is. Both carry the number at full strength.
    _shapes.FillRect(badgeX, badgeTop, badgeWidth, BADGE_HEIGHT, yours ? color : WithAlpha(color, BADGE_RIVAL_ALPHA));
    _text.DrawText(static_cast<std::int32_t>(badgeX) + static_cast<std::int32_t>(BADGE_PADDING), labelY, ships,
                   yours ? Ink::APP_BACKGROUND : color);

    // A rival's badge names the system, so the tap focuses it exactly as the disc does. Yours names
    // the fleets standing there, which is a different thing to tap and a different index (ADR-057).
    //
    // **Drawn at 16, hit at the touch floor** (ADR-098, ADR-100). A badge is an isolated chip -- it
    // has the pane around it and is already placed clear of the disc and of its neighbour -- so the
    // target grows and the drawing does not. A 44px badge beside a system name would be a different
    // map rather than a bigger box. The rectangle is centred on what is drawn, so where a finger
    // aims and where the eye aims are the same point.
    // **The origin wears an outline while its fleet is being moved** (ADR-113), so the question
    // *where is this going FROM* is answered on the map rather than only in the banner.
    if (yours && _index == _frame.moveOrigin)
    {
      _shapes.StrokeRect(badgeX - MOVE_ORIGIN_OFFSET - MOVE_ORIGIN_OUTLINE, badgeTop - MOVE_ORIGIN_OFFSET - MOVE_ORIGIN_OUTLINE,
                         badgeWidth + 2.0F * (MOVE_ORIGIN_OFFSET + MOVE_ORIGIN_OUTLINE),
                         BADGE_HEIGHT + 2.0F * (MOVE_ORIGIN_OFFSET + MOVE_ORIGIN_OUTLINE), Ink::TEXT_PRIMARY, MOVE_ORIGIN_OUTLINE);
    }

    const float hitWidth = std::max(badgeWidth, BADGE_TOUCH_FLOOR);
    if (!moving)
    {
      _hits.push_back(MapHit{.x = badgeX - (hitWidth - badgeWidth) * 0.5F,
                             .y = badgeTop - (BADGE_TOUCH_FLOOR - BADGE_HEIGHT) * 0.5F,
                             .width = hitWidth,
                             .height = BADGE_TOUCH_FLOOR,
                             .system = yours ? EventRefs::NONE : _index,
                             .fleetsAt = yours ? _index : EventRefs::NONE});
    }
    // The badge is drawn ink competing for the same strip as the next system's name, so it joins
    // the field rather than only avoiding it.
    _labels.placed.push_back(LabelField::Box{badgeX, badgeTop, badgeX + badgeWidth, badgeTop + BADGE_HEIGHT});
    badgeX += badgeWidth + 3.0F;
  }
}

/// Where along its lane a fleet is DRAWN, which is where it is except near the ends.
///
/// The clamp is computed in SCREEN pixels and applied to the design-space fraction, because what
/// has to be cleared is drawn at a fixed size at every zoom -- a node's radius and a label's
/// glyphs -- while the lane's length in pixels changes with the camera.
[[nodiscard]] float DrawnProgress(const Neuron::OrbitCamera& _camera, const SystemNode& _from, const SystemNode& _to, float _progress)
{
  const Neuron::OrbitCamera::ScreenPoint a = _camera.Project(MapView::Ground(_from.positionX, _from.positionY));
  const Neuron::OrbitCamera::ScreenPoint b = _camera.Project(MapView::Ground(_to.positionX, _to.positionY));
  if (!a.visible || !b.visible)
  {
    return _progress;
  }

  const float runX = b.xPixels - a.xPixels;
  const float runY = b.yPixels - a.yPixels;
  const float length = std::sqrt(runX * runX + runY * runY);
  if (length <= 2.0F * FLEET_END_CLEARANCE)
  {
    return 0.5F;
  }

  const float margin = FLEET_END_CLEARANCE / length;
  return std::clamp(_progress, margin, 1.0F - margin);
}

/// Where a fleet in transit is drawn this frame: its foot on the lane and its head above it,
/// computed the same way for both halves of drawing it.
struct FleetMarker
{
  bool visible = false;
  float designX = 0.0F;
  float designY = 0.0F;
  Neuron::OrbitCamera::ScreenPoint foot = {};
  Neuron::OrbitCamera::ScreenPoint head = {};
};

[[nodiscard]] FleetMarker PlaceFleet(const MapFrame& _frame, const Fleet& _fleet)
{
  const Neuron::OrbitCamera& camera = _frame.view.Camera();
  const SystemNode& from = _frame.state.graph.systems[static_cast<std::size_t>(_fleet.from)];
  const SystemNode& to = _frame.state.graph.systems[static_cast<std::size_t>(_fleet.to)];
  const float drawnAt = DrawnProgress(camera, from, to, _fleet.progress);

  FleetMarker marker;
  marker.designX = from.positionX + (to.positionX - from.positionX) * drawnAt;
  marker.designY = from.positionY + (to.positionY - from.positionY) * drawnAt;
  marker.foot = camera.Project(MapView::Ground(marker.designX, marker.designY));
  marker.head = camera.Project(MapView::Above(marker.designX, marker.designY, FLEET_HOVER));
  marker.visible = marker.foot.visible && marker.head.visible;
  return marker;
}

/// The fleet's tether into the shape recorder and its dart into the mesh recorder, with the
/// stations' stems and balls (ADR-106).
void DrawFleetGround(ShapeRenderer& _shapes, Neuron::MeshRenderer& _meshes, const MapFrame& _frame, std::int32_t _index)
{
  const Fleet& fleet = _frame.state.fleets[static_cast<std::size_t>(_index)];
  const FleetMarker marker = PlaceFleet(_frame, fleet);
  if (!marker.visible)
  {
    return;
  }

  const Color owner = OwnerColor(fleet.owner, _frame.state.viewer);
  _shapes.Line(marker.foot.xPixels, marker.foot.yPixels, marker.head.xPixels, marker.head.yPixels, WithAlpha(owner, 153));

  // The dart points along the lane IN WORLD SPACE, so it turns with the camera and keeps meaning
  // "that way" rather than "that way on the screen when the map happened to be seen from the
  // front" -- which is what the projected arrowhead it replaces was careful about too.
  const SystemNode& from = _frame.state.graph.systems[static_cast<std::size_t>(fleet.from)];
  const SystemNode& to = _frame.state.graph.systems[static_cast<std::size_t>(fleet.to)];
  const Neuron::OrbitCamera::WorldPoint ahead = {to.positionX - from.positionX, 0.0F, to.positionY - from.positionY};

  // A flat-faced solid, so no rim and no glint: its faces are oblique across their whole area and
  // would trip both (`Ink::FlatRampFor`).
  _meshes.Octahedron(MapView::Above(marker.designX, marker.designY, FLEET_HOVER), ahead, FLEET_HALF_LENGTH, FLEET_HALF_WIDTH,
                     FLEET_HALF_HEIGHT, Ink::FlatRampFor(BallTone(owner)));
}

/// The label and the hit, over the balls. The marker itself is a solid now and is drawn with them
/// (ADR-106); what stays here is the ink a ball must not cover.
void DrawFleetOverlay(FontRenderer& _text, const MapFrame& _frame, std::vector<MapHit>& _hits, LabelField& _labels, std::int32_t _index)
{
  const Fleet& fleet = _frame.state.fleets[static_cast<std::size_t>(_index)];
  const FleetMarker marker = PlaceFleet(_frame, fleet);
  if (!marker.visible)
  {
    return;
  }

  const Neuron::OrbitCamera::ScreenPoint& head = marker.head;
  const Color owner = OwnerColor(fleet.owner, _frame.state.viewer);

  // Two fleets converging on one system put their labels in the same place -- which is what is
  // happening at Kepler-Reach in the reference, and is the normal case rather than an edge one.
  // Yours goes above the arrowhead, where you are already looking; a rival's goes beside and below
  // it, so the two can never overlap.
  const std::string label = std::format("{} · ETA T{}", fleet.name, fleet.eta);
  const auto labelWidth = static_cast<float>(FontRenderer::MeasurePixels(label));
  const auto labelY = static_cast<std::int32_t>(std::lround(head.yPixels - 18.0F));
  const float paneX = Frame::DIGEST_WIDTH;
  const float paneWidth = Frame::SCREEN_WIDTH - Frame::DIGEST_WIDTH - Frame::ORDERS_WIDTH;

  if (fleet.owner == _frame.state.viewer)
  {
    const float clamped = std::clamp(head.xPixels, paneX + labelWidth * 0.5F + 4.0F, paneX + paneWidth - labelWidth * 0.5F - 4.0F);
    DrawCentered(_text, clamped, _labels.Place(clamped, labelY, FontRenderer::MeasurePixels(label)), label, owner);

    // **No marker is a target while a move is being chosen** (ADR-113): the only things the map
    // offers then are the systems this fleet may be sent to, and a tap anywhere else leaves the
    // mode.
    if (_frame.moveOrigin == EventRefs::NONE)
    {
      _hits.push_back(MapHit{.x = head.xPixels - 14.0F, .y = head.yPixels - 22.0F, .width = 28.0F, .height = 36.0F, .fleet = _index});
    }
  }
  else
  {
    const float placed = std::min(head.xPixels + 10.0F, paneX + paneWidth - labelWidth - 4.0F);
    const std::int32_t at = _labels.Place(placed + labelWidth * 0.5F, labelY + LINE_HEIGHT, FontRenderer::MeasurePixels(label));
    _text.DrawText(static_cast<std::int32_t>(std::lround(placed)), at, label, owner);
  }
}

/// One frame of the map, and everything its phases share (ADR-014, ADR-017).
///
/// **The camera is what makes this a record rather than seven free functions over a `MapFrame`.**
/// Projecting a world point, finding a system's spot on the plane and drawing a segment between two
/// projected points are the three things every phase does, all three need the camera the frame was
/// just framed with, and passing that camera plus a hit list plus a label field to eight functions
/// is the argument list a record exists to replace.
struct MapPass
{
  const MapFrame& frame;
  const Neuron::OrbitCamera& camera;
  /// Where the taps this frame records go, and the field that keeps the names out of each other's
  /// way (ADR-090). Both are written by the phases and read by the caller.
  std::vector<MapHit>& hits;
  LabelField& labels;
  /// The map pane's left edge, which the focus caption and the legend both start from. The width
  /// and the height are the viewport's, set once in `BeginMap` and never read again.
  float paneX = 0.0F;
  /// Whether a move is being chosen on this map, which changes what every phase draws (ADR-113).
  bool moving = false;

  [[nodiscard]] Neuron::OrbitCamera::ScreenPoint Project(const Neuron::OrbitCamera::WorldPoint& _world) const
  {
    return camera.Project(_world);
  }

  /// Where a system stands on the plane, projected.
  [[nodiscard]] Neuron::OrbitCamera::ScreenPoint GroundOf(std::int32_t _system) const
  {
    const SystemNode& node = frame.state.graph.systems[static_cast<std::size_t>(_system)];
    return Project(MapView::Ground(node.positionX, node.positionY));
  }

  /// A segment between two projected points, drawn only when both ends are in front of the eye.
  ///
  /// Clipping the one-end case properly would be the right answer for a camera that can be put
  /// inside the galaxy; this one orbits outside it, so a lane with one end behind the eye is a
  /// lane at the very edge of a steep view, and dropping it is not visible.
  void Segment(ShapeRenderer& _shapes, const Neuron::OrbitCamera::ScreenPoint& _a, const Neuron::OrbitCamera::ScreenPoint& _b,
               const Color& _color, float _thickness) const
  {
    if (_a.visible && _b.visible)
    {
      _shapes.Line(_a.xPixels, _a.yPixels, _b.xPixels, _b.yPixels, _color, _thickness);
    }
  }
};

/// The pane, the camera framed to the board, the sky, and the record the phases share.
///
/// Framed to the tallest stem rather than to a constant (ADR-103), and the mesh pass is given the
/// same camera here so that the balls and their labels are placed by one.
[[nodiscard]] MapPass BeginMap(ShapeRenderer& _shapes, FontRenderer& _text, Neuron::MeshRenderer& _meshes, const MapFrame& _frame,
                               std::vector<MapHit>& _hits, LabelField& _labels)
{
  const float paneX = Frame::DIGEST_WIDTH;
  const float paneWidth = Frame::SCREEN_WIDTH - Frame::DIGEST_WIDTH - Frame::ORDERS_WIDTH;
  const float paneHeight = Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT;
  _frame.view.SetViewport(paneX, Frame::TOP_BAR_HEIGHT, paneWidth, paneHeight);

  // Framed to the tallest stem on the board, not to a constant: height is the yield now (ADR-103),
  // and a camera framed to the old capital height would put the richest system's ball above the
  // pane. Never less than a capital's floor, so an unpriced board frames as it always did.
  float tallest = CAPITAL_STEM_HEIGHT;
  for (const SystemNode& node : _frame.state.graph.systems)
  {
    if (!HasFlag(node.flags, SystemFlags::RegionAnchor))
    {
      tallest = std::max(tallest, StemHeightFor(node.production, HasFlag(node.flags, SystemFlags::Capital)));
    }
  }
  _frame.view.FrameContent(_frame.contentCenter, _frame.contentRadius, tallest + CAPITAL_RADIUS);
  const Neuron::OrbitCamera& camera = _frame.view.Camera();

  // The same camera, handed to the mesh pass as a matrix, with the light and the pane it projects
  // into. Set here, after the framing, so the balls and their labels are placed by one camera.
  _meshes.SetView(Neuron::MeshRenderer::View{.viewProjection = camera.ViewProjection(),
                                             .lightDirection = LIGHT_DIRECTION,
                                             .eyePosition = camera.Position(),
                                             .viewportXPixels = paneX,
                                             .viewportYPixels = Frame::TOP_BAR_HEIGHT,
                                             .viewportWidthPixels = paneWidth,
                                             .viewportHeightPixels = paneHeight});

  _shapes.FillVerticalGradient(paneX, Frame::TOP_BAR_HEIGHT, paneWidth, paneHeight, MAP_TOP, MAP_MIDDLE, 0.45F, MAP_BOTTOM);
  _text.SetClipRect(paneX, Frame::TOP_BAR_HEIGHT, paneWidth, paneHeight);

  // The sky goes through the same camera as everything else, because it is in the same world --
  // infinitely far away in it, which is a direction rather than a place (ADR-032). It is drawn
  // first and depth-tests against nothing, so the galaxy covers it.
  _frame.sky.Draw(_shapes, camera, STAR);

  return MapPass{
    .frame = _frame, .camera = camera, .hits = _hits, .labels = _labels, .paneX = paneX, .moving = _frame.moveOrigin != EventRefs::NONE};
}

void DrawGroundGrid(ShapeRenderer& _shapes, const MapPass& _pass)
{
  // The ground grid, now genuinely on the ground: lines of constant x and constant z, projected.
  // It turns with the camera because it is part of the world, and that single change is most of
  // what makes the rotation read as a viewpoint moving rather than a picture being spun (ADR-017).
  for (std::uint32_t step = 0; step <= GRID_LINES_ACROSS; ++step)
  {
    const float at = GRID_MIN_DESIGN + static_cast<float>(step) * GRID_STEP;
    _pass.Segment(_shapes, _pass.Project(MapView::Ground(at, GRID_MIN_DESIGN)), _pass.Project(MapView::Ground(at, GRID_MAX_DESIGN)),
                  GRID_LINE, 1.0F);
    _pass.Segment(_shapes, _pass.Project(MapView::Ground(GRID_MIN_DESIGN, at)), _pass.Project(MapView::Ground(GRID_MAX_DESIGN, at)),
                  GRID_LINE, 1.0F);
  }
}

void DrawLanes(ShapeRenderer& _shapes, FontRenderer& _text, const MapPass& _pass)
{
  // Lanes lie on the plane, under everything that stands on it.
  for (const Lane& lane : _pass.frame.state.graph.lanes)
  {
    const Neuron::OrbitCamera::ScreenPoint a = _pass.GroundOf(lane.a);
    const Neuron::OrbitCamera::ScreenPoint b = _pass.GroundOf(lane.b);
    if (!a.visible || !b.visible)
    {
      continue;
    }
    _pass.labels.lanes.push_back(LabelField::Segment{a.xPixels, a.yPixels, b.xPixels, b.yPixels});

    // **While a move is being chosen, a lane is either an offer or it is out of the way**
    // (ADR-113). The ones out of the origin that lead somewhere the fleet may go are drawn in blue
    // with the dashes marching toward the destination, and the one already chosen is solid; every
    // other lane on the plane drops to a hairline, so the blue on the map is the choice and nothing
    // else. The tick cost drops with its lane, because the chip under the lit system's name carries
    // the same number and says the arrival tick too.
    const MoveTarget* offered = nullptr;
    if (_pass.moving)
    {
      const std::int32_t other = lane.a == _pass.frame.moveOrigin ? lane.b : (lane.b == _pass.frame.moveOrigin ? lane.a : EventRefs::NONE);
      offered = other == EventRefs::NONE ? nullptr : TargetFor(_pass.frame, other);
    }

    if (_pass.moving && offered == nullptr)
    {
      _shapes.Line(a.xPixels, a.yPixels, b.xPixels, b.yPixels, LANE_UNLIT, 1.2F);
      DrawCentered(_text, (a.xPixels + b.xPixels) * 0.5F, static_cast<std::int32_t>(std::lround((a.yPixels + b.yPixels) * 0.5F)) - 4,
                   std::to_string(lane.cost), LANE_UNLIT);
      continue;
    }
    if (offered != nullptr)
    {
      if (offered->system == _pass.frame.moveSelected)
      {
        _shapes.Line(a.xPixels, a.yPixels, b.xPixels, b.yPixels, Ink::BLUE, 2.5F);
      }
      else
      {
        _shapes.DashedLine(a.xPixels, a.yPixels, b.xPixels, b.yPixels, Ink::BLUE, 1.5F, MOVE_DASH, MOVE_GAP,
                           _pass.frame.animationSeconds * MOVE_DASH_PIXELS_PER_SECOND);
      }
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
}

void DrawRoutes(ShapeRenderer& _shapes, const MapPass& _pass)
{
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
  for (const Fleet& fleet : _pass.frame.state.fleets)
  {
    if (!fleet.OnALane())
    {
      continue;
    }

    const Neuron::OrbitCamera::ScreenPoint origin = _pass.GroundOf(fleet.from);
    const Neuron::OrbitCamera::ScreenPoint destination = _pass.GroundOf(fleet.to);
    if (!origin.visible || !destination.visible)
    {
      continue;
    }

    _shapes.DashedLine(origin.xPixels, origin.yPixels, destination.xPixels, destination.yPixels,
                       WithAlpha(OwnerColor(fleet.owner, _pass.frame.state.viewer), ROUTE_ALPHA), ROUTE_THICKNESS, ROUTE_DOT, ROUTE_GAP,
                       _pass.frame.animationSeconds * ROUTE_SPEED_PIXELS_PER_SECOND);
  }
}

void DrawSealedRegion(ShapeRenderer& _shapes, FontRenderer& _text, const MapPass& _pass)
{
  // The sealed region: everyone can see it and count down to it, which is what makes it a race
  // rather than a reward (one-pager, "Pacing devices").
  //
  // It is a CIRCLE on the ground, emitted as a projected polygon rather than as a screen-space
  // ellipse. An ellipse was right when the viewing angle could not change; now the shape a ground
  // circle makes depends on where the camera is, and projecting it is how it comes out right at
  // every angle for free.
  if (_pass.frame.state.region.anchor != EventRefs::NONE)
  {
    const SystemNode& anchor = _pass.frame.state.graph.systems[static_cast<std::size_t>(_pass.frame.state.region.anchor)];
    DrawGroundCircle(_shapes, _pass.frame, anchor.positionX, anchor.positionY, REGION_RADIUS, WithAlpha(Ink::PURPLE, 20), Ink::PURPLE,
                     true);
    // A second ring, lifted. The reference drew one to suggest a volume rather than a puddle, and
    // with a real camera it does the job properly: it is a circle at altitude, so the gap between
    // the two rings opens and closes as the camera tilts.
    DrawGroundCircle(_shapes, _pass.frame, anchor.positionX, anchor.positionY, REGION_RADIUS, {0, 0, 0, 0}, WithAlpha(Ink::PURPLE, 89),
                     true, REGION_VOLUME_HEIGHT);

    for (const auto& [offsetX, offsetY] : _pass.frame.state.region.siteOffsets)
    {
      const Neuron::OrbitCamera::ScreenPoint foot = _pass.Project(MapView::Ground(anchor.positionX + offsetX, anchor.positionY + offsetY));
      const Neuron::OrbitCamera::ScreenPoint head =
        _pass.Project(MapView::Above(anchor.positionX + offsetX, anchor.positionY + offsetY, SITE_PIN_HEIGHT));
      _pass.Segment(_shapes, foot, head, WithAlpha(Ink::PURPLE, 153), 1.0F);
      if (head.visible)
      {
        _shapes.FillEllipse(head.xPixels, head.yPixels, 2.5F, 2.5F, Ink::PURPLE);
      }
    }

    // Below the nearest point of the rim, not beside the centre: at a low camera angle the two
    // are almost the same place and the label lands inside the region.
    const Neuron::OrbitCamera::ScreenPoint label = _pass.Project(MapView::Ground(anchor.positionX, anchor.positionY + REGION_RADIUS));
    if (label.visible)
    {
      DrawCentered(_text, label.xPixels, static_cast<std::int32_t>(std::lround(label.yPixels)) + 14,
                   std::format("SEALED - OPENS T{}", _pass.frame.state.region.opensAt), Ink::PURPLE);
    }
  }
}

void DrawSolids(ShapeRenderer& _shapes, FontRenderer& _text, Neuron::MeshRenderer& _meshes, MapPass& _pass)
{
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
  drawables.reserve(_pass.frame.state.graph.systems.size() + _pass.frame.state.fleets.size());

  for (std::size_t index = 0; index < _pass.frame.state.graph.systems.size(); ++index)
  {
    const SystemNode& node = _pass.frame.state.graph.systems[index];
    if (HasFlag(node.flags, SystemFlags::RegionAnchor))
    {
      continue;
    }
    const Neuron::OrbitCamera::ScreenPoint ground = _pass.GroundOf(static_cast<std::int32_t>(index));
    if (ground.visible)
    {
      drawables.push_back(Drawable{ground.depth, static_cast<std::int32_t>(index), false});
    }
  }

  for (std::size_t index = 0; index < _pass.frame.state.fleets.size(); ++index)
  {
    const Fleet& fleet = _pass.frame.state.fleets[index];
    if (!fleet.OnALane())
    {
      continue;
    }
    // The same position `DrawFleet` will use, clamp included: a depth sorted from one point and
    // drawn at another puts a fleet in front of a system it is behind.
    const SystemNode& from = _pass.frame.state.graph.systems[static_cast<std::size_t>(fleet.from)];
    const SystemNode& to = _pass.frame.state.graph.systems[static_cast<std::size_t>(fleet.to)];
    const float drawnAt = DrawnProgress(_pass.camera, from, to, fleet.progress);
    const float designX = from.positionX + (to.positionX - from.positionX) * drawnAt;
    const float designY = from.positionY + (to.positionY - from.positionY) * drawnAt;
    const Neuron::OrbitCamera::ScreenPoint at = _pass.Project(MapView::Ground(designX, designY));
    if (at.visible)
    {
      drawables.push_back(Drawable{at.depth, static_cast<std::int32_t>(index), true});
    }
  }

  std::sort(drawables.begin(), drawables.end(), [](const Drawable& _a, const Drawable& _b) { return _a.depth > _b.depth; });

  // Two passes over the same order, with the layer boundary between them (ADR-103). Everything
  // UNDER a ball first -- shadows, discs, footprints, stems -- and the balls themselves into the
  // mesh recorder; then the boundary; then everything OVER a ball -- rings, arrowheads, names,
  // badges -- so that the mesh pass drawn between the two shape layers lands exactly where the
  // mockup puts it. The balls are depth-tested against nothing but other balls; every flat thing
  // still layers by this order.
  for (const Drawable& drawable : drawables)
  {
    if (drawable.isFleet)
    {
      DrawFleetGround(_shapes, _meshes, _pass.frame, drawable.index);
    }
    else
    {
      DrawStationGround(_shapes, _meshes, _pass.frame, drawable.index);
    }
  }

  _shapes.EndLayer();

  for (const Drawable& drawable : drawables)
  {
    if (drawable.isFleet)
    {
      DrawFleetOverlay(_text, _pass.frame, _pass.hits, _pass.labels, drawable.index);
    }
    else
    {
      DrawStationOverlay(_shapes, _text, _pass.frame, _pass.hits, _pass.labels, drawable.index);
    }
  }
}

void DrawFocusCaption(FontRenderer& _text, const MapPass& _pass)
{
  // `MAP - FOCUS: HALVORSEN` in the top-left corner (DESIGN-GUIDELINES "Map"). The map is no
  // longer captioned with a census -- that is on the top bar now -- and says instead what it is
  // currently pointed at, because the digest can point it somewhere.
  //
  // **The move mode's banner takes this corner** (ADR-113), so it is not drawn under one: the
  // banner is interface and this is world text, and interface is drawn second -- a caption left
  // here would be a caption the banner cannot cover.
  if (!_pass.moving)
  {
    _text.DrawText(static_cast<std::int32_t>(_pass.paneX) + 12, static_cast<std::int32_t>(Frame::TOP_BAR_HEIGHT) + 12,
                   FocusLine(_pass.frame.state, _pass.frame.focusedSystem), Ink::TEXT_DETAIL);
  }
}

void DrawLegend(ShapeRenderer& _shapes, FontRenderer& _text, const MapPass& _pass)
{
  // The legend earns its place: the owner colours are also the semantic colours, so a player who
  // learns this row can read every other coloured thing on the screen.
  struct LegendEntry
  {
    std::string label;
    Color color;
    bool isLane;
    bool dashed;
    /// A garrison badge rather than a dot or a lane: the legend draws the shape it is naming, and a
    /// badge is a filled chip (ADR-079).
    bool isBadge = false;
    /// A fleet under way, which is an arrowhead on the map and so an arrowhead here (ADR-090). It
    /// wore the route's dashes, which is the thing the fleet travels along rather than the fleet.
    bool isFleet = false;
  };

  // The empires this player can actually see, not all twelve. A twelve-swatch legend would fill
  // the bar with colours for empires nobody has met, and the
  // entries that earn their place are the ones already on the map (ADR-027).
  const auto labelOf = [&_pass](OwnerId _player)
  {
    return _player >= 0 && _player < static_cast<OwnerId>(_pass.frame.state.players.size())
             ? _pass.frame.state.players[static_cast<std::size_t>(_player)].label
             : std::string("RIVAL");
  };

  std::vector<LegendEntry> legend;
  legend.push_back({labelOf(_pass.frame.state.viewer), OwnerColor(_pass.frame.state.viewer, _pass.frame.state.viewer), false, false});

  std::vector<OwnerId> rivals;
  for (const SystemNode& node : _pass.frame.state.graph.systems)
  {
    if (node.owner != NOBODY && node.owner != _pass.frame.state.viewer &&
        std::find(rivals.begin(), rivals.end(), node.owner) == rivals.end())
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
    legend.push_back({labelOf(rivals[index]), OwnerColor(rivals[index], _pass.frame.state.viewer), false, false});
  }

  legend.push_back({"PROPOSED LANE", Ink::BLUE, true, true});
  legend.push_back({"TRADE LANE", Ink::BLUE, true, false});

  // Only when there is one on the map. A legend entry for a thing nobody can see is a colour to
  // learn for nothing, which is the rule the rival swatches above already follow (ADR-027).
  const bool anyMoving =
    std::any_of(_pass.frame.state.fleets.begin(), _pass.frame.state.fleets.end(), [](const Fleet& _fleet) { return _fleet.OnALane(); });
  if (anyMoving)
  {
    legend.push_back({"FLEET UNDER WAY", Ink::BLUE, false, false, false, true});
  }

  // **`SHIPS`, not `FLEETS`, because the number on the badge is ships.** A system holding three
  // fleets of three wears one badge reading 9, and a legend calling that "fleets" would be teaching
  // the wrong reading of the only number the map now carries.
  const bool anyHolding = std::any_of(_pass.frame.state.fleets.begin(), _pass.frame.state.fleets.end(),
                                      [](const Fleet& _fleet) { return !_fleet.OnALane() && _fleet.owner != NOBODY; });
  if (anyHolding)
  {
    legend.push_back({"SHIPS HOLDING", Ink::BLUE, false, false, true});
  }

  // **Not under a sheet** (ADR-082). The legend's row is the bottom twenty pixels of the pane and a
  // sheet's `CANCEL` bar is the bottom fifty-two, so every sheet capture this project has taken
  // shows `YOU  PROPOSED LANE  TRADE LANE` sliced off under it. A legend nobody can read is worse
  // than no legend: it is a row of half-glyphs that looks like a rendering fault.
  float legendX = _pass.paneX + 12.0F;
  const float legendY = Frame::SCREEN_HEIGHT - 20.0F;
  for (const LegendEntry& entry : _pass.frame.sheetOpen ? std::vector<LegendEntry>{} : legend)
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
    else if (entry.isBadge)
    {
      _shapes.FillRect(legendX, legendY - 1.0F, 10.0F, 10.0F, entry.color);
      legendX += 15.0F;
    }
    else if (entry.isFleet)
    {
      // The same arrowhead `DrawFleet` puts on a lane, pointing right: a fleet is a triangle and a
      // system is a disc, which is the difference the legend exists to teach (ADR-090).
      _shapes.FillTriangle(legendX + 9.0F, legendY + 4.0F, legendX, legendY - 1.0F, legendX, legendY + 9.0F, entry.color);
      legendX += 14.0F;
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
}

} // namespace

std::string FocusLine(const MatchState& _state, std::int32_t _focusedSystem)
{
  std::string line = "MAP";
  if (_focusedSystem != EventRefs::NONE && _focusedSystem < static_cast<std::int32_t>(_state.graph.systems.size()))
  {
    const SystemNode& focused = _state.graph.systems[static_cast<std::size_t>(_focusedSystem)];
    line += focused.name.empty() ? " - FOCUS: THE FALLOW" : " - FOCUS: " + Uppercased(focused.name);
  }
  return line;
}

bool CaptureIsNews(std::uint32_t _capturedAt, std::uint32_t _tick) noexcept
{
  /// Three ticks is the window a returning player is shown anyway (ADR-044's backlog is counted in
  /// ticks, and a digest reports the tick it is about), so a label that outlives it is saying
  /// something no card is still saying.
  constexpr std::uint32_t CAPTURE_NEWS_TICKS = 3;
  return _capturedAt != 0 && _tick <= _capturedAt + CAPTURE_NEWS_TICKS;
}

float StemHeightFor(std::uint32_t _production, bool _capital) noexcept
{
  if (_production == 0)
  {
    return _capital ? CAPITAL_STEM_HEIGHT : STEM_HEIGHT;
  }
  const float stands = STEM_BASE + static_cast<float>(_production) * STEM_PER_UNIT;
  return _capital ? std::max(stands, CAPITAL_STEM_HEIGHT) : stands;
}

float FootprintRadiusFor(std::uint32_t _production) noexcept
{
  return FOOTPRINT_BASE + static_cast<float>(_production) * FOOTPRINT_PER_UNIT;
}

/// One frame of the map: the ground, the lanes, the routes, the sealed region, the systems and
/// fleets back to front, the focus caption and the legend (ADR-017, ADR-103).
std::vector<MapHit> DrawMap(ShapeRenderer& _shapes, FontRenderer& _text, Neuron::MeshRenderer& _meshes, const MapFrame& _frame)
{
  std::vector<MapHit> hits;
  LabelField labels;
  MapPass pass = BeginMap(_shapes, _text, _meshes, _frame, hits, labels);

  // Ground first, then what lies on it, then what stands on it, then what is written over it. The
  // order IS the occlusion model: this renderer has no depth buffer for interface geometry, so a
  // phase drawn later is a phase drawn on top (ADR-014).
  DrawGroundGrid(_shapes, pass);
  DrawLanes(_shapes, _text, pass);
  DrawRoutes(_shapes, pass);
  DrawSealedRegion(_shapes, _text, pass);
  DrawSolids(_shapes, _text, _meshes, pass);
  DrawFocusCaption(_text, pass);
  DrawLegend(_shapes, _text, pass);

  return hits;
}

} // namespace Lockstep
