#pragma once

#include "MeshRenderer.h"

namespace Frontier
{

// The station: a fixed structure the ship flies around, embedded in the binary (R13).
//
// GAME content, like the ship, and for the same reason (R9): the engine draws meshes and has no
// idea what a station is. What crosses the boundary is Neuron::MeshVertex.
//
// It is STATIC. It is not in the simulation, the server does not know about it and it has no
// state -- it is a world matrix and a mesh. When it becomes something the ship can dock with, it
// becomes an entity in GameLogic and this stays what draws it.
//
// Unlike the ship, it is GENERATED rather than authored face by face, because its plan is a
// regular octagon and eight-fold symmetry written out by hand is sixty-four chances to mistype a
// coordinate. The generator runs at compile time, so the array in the binary is still a constant
// and there is still no code that builds geometry at runtime.
//
// The shape, bottom to top: a wide octagonal base drum, a narrower tower rising out of it, a flat
// cap, and four solar panels on the tower. It is deliberately a stack of tiers rather than a
// single block -- ADR-002's lighting only reads as three-dimensional when a mesh has faces at
// clearly different orientations, and eight vertical facets around a drum is exactly that: the
// light rakes across them and splits the ring into a lit half and a shaded half.

/// A point in the station's model space. Local to this file: the engine has no general vector
/// type, and it does not need one to draw a mesh.
struct StationPoint
{
  float x;
  float y;
  float z;
};

/// The eight compass directions of the octagonal plan, as unit vectors in the ground plane.
/// Written out rather than computed: std::cos is not usable in a constexpr, and these eight pairs
/// are exactly the values it would return.
inline constexpr std::size_t STATION_SIDES = 8;
inline constexpr float STATION_DIAGONAL = 0.70710678F;
inline constexpr std::array<StationPoint, STATION_SIDES> STATION_DIRECTIONS = {{
  {1.0F, 0.0F, 0.0F},
  {STATION_DIAGONAL, 0.0F, STATION_DIAGONAL},
  {0.0F, 0.0F, 1.0F},
  {-STATION_DIAGONAL, 0.0F, STATION_DIAGONAL},
  {-1.0F, 0.0F, 0.0F},
  {-STATION_DIAGONAL, 0.0F, -STATION_DIAGONAL},
  {0.0F, 0.0F, -1.0F},
  {STATION_DIAGONAL, 0.0F, -STATION_DIAGONAL},
}};

// The tiers, in metres. The station is parameterized rather than sculpted, so resizing it is
// these eight numbers rather than a scale factor like the ship's.
//
// The widest part is the base drum, and a drum of radius r spans r * sqrt(2) * 8 virtual pixels
// across -- the sqrt(2) because the octagon's widest diagonal runs corner to corner, and the 8
// because that is the camera's pixels per ground unit (ADR-003). At radius 4.5 that is 51 pixels
// of the 640, and 9 m tall is 72. Getting this arithmetic wrong is what put the first version,
// radius 11 by 22 m, off the left edge of the screen.
inline constexpr float STATION_BASE_RADIUS = 4.5F;
inline constexpr float STATION_BASE_TOP = 2.0F;
inline constexpr float STATION_TOWER_RADIUS = 2.0F;
inline constexpr float STATION_TOWER_TOP = 9.0F;
inline constexpr float STATION_PANEL_HEIGHT = 5.5F;
inline constexpr float STATION_PANEL_INNER = 2.25F;
inline constexpr float STATION_PANEL_OUTER = 6.0F;
inline constexpr float STATION_PANEL_HALF_WIDTH = 0.7F;

/// The same three palette pairs the ship uses, so the two read as built by the same people
/// (ADR-002; the authored index is the dark half, 0-7).
inline constexpr std::uint32_t STATION_STRUCTURE_COLOR = 7; // AAAAAA, lighting to FFFFFF
inline constexpr std::uint32_t STATION_BEACON_COLOR = 4;    // AA0000, lighting to FF5555
inline constexpr std::uint32_t STATION_PANEL_COLOR = 1;     // 0000AA, lighting to 5555FF

// 8 base sides + 8 base-top ring + 8 tower sides, two triangles each, then 8 cap triangles and 4
// panels of two. Counted here so the array size and the generator cannot disagree.
inline constexpr std::size_t STATION_PANEL_COUNT = 4;
inline constexpr std::size_t STATION_TRIANGLE_COUNT = (STATION_SIDES * 3) * 2 + STATION_SIDES + STATION_PANEL_COUNT * 2;
inline constexpr std::size_t STATION_VERTEX_COUNT = STATION_TRIANGLE_COUNT * 3;

/// Builds the station at compile time.
///
/// Every face is wound so that the cross product of its first two edges points OUT of the solid,
/// which is what makes the normals correct without any of them being written down.
[[nodiscard]] inline constexpr std::array<Neuron::MeshVertex, STATION_VERTEX_COUNT> BuildStationVertices() noexcept
{
  std::array<Neuron::MeshVertex, STATION_VERTEX_COUNT> vertices = {};
  std::size_t used = 0;

  const auto ring = [](std::size_t _side, float _radius, float _height)
  {
    const StationPoint& direction = STATION_DIRECTIONS[_side % STATION_SIDES];
    return StationPoint{direction.x * _radius, _height, direction.z * _radius};
  };

  const auto triangle = [&vertices, &used](const StationPoint& _a, const StationPoint& _b, const StationPoint& _c, std::uint32_t _color)
  {
    const float edge1X = _b.x - _a.x;
    const float edge1Y = _b.y - _a.y;
    const float edge1Z = _b.z - _a.z;
    const float edge2X = _c.x - _a.x;
    const float edge2Y = _c.y - _a.y;
    const float edge2Z = _c.z - _a.z;

    const float normalX = edge1Y * edge2Z - edge1Z * edge2Y;
    const float normalY = edge1Z * edge2X - edge1X * edge2Z;
    const float normalZ = edge1X * edge2Y - edge1Y * edge2X;

    vertices[used++] = {_a.x, _a.y, _a.z, normalX, normalY, normalZ, _color};
    vertices[used++] = {_b.x, _b.y, _b.z, normalX, normalY, normalZ, _color};
    vertices[used++] = {_c.x, _c.y, _c.z, normalX, normalY, normalZ, _color};
  };

  const auto quad =
    [&triangle](const StationPoint& _a, const StationPoint& _b, const StationPoint& _c, const StationPoint& _d, std::uint32_t _color)
  {
    triangle(_a, _b, _c, _color);
    triangle(_a, _c, _d, _color);
  };

  for (std::size_t side = 0; side < STATION_SIDES; ++side)
  {
    // The drum walls, facing outwards. Bottom then top of this facet, then top and bottom of the
    // next: that order is the one whose normal points away from the axis.
    quad(ring(side, STATION_BASE_RADIUS, 0.0F), ring(side, STATION_BASE_RADIUS, STATION_BASE_TOP),
         ring(side + 1, STATION_BASE_RADIUS, STATION_BASE_TOP), ring(side + 1, STATION_BASE_RADIUS, 0.0F), STATION_STRUCTURE_COLOR);

    // The walkway on top of the base, between the drum's rim and the tower, facing up.
    quad(ring(side, STATION_BASE_RADIUS, STATION_BASE_TOP), ring(side, STATION_TOWER_RADIUS, STATION_BASE_TOP),
         ring(side + 1, STATION_TOWER_RADIUS, STATION_BASE_TOP), ring(side + 1, STATION_BASE_RADIUS, STATION_BASE_TOP),
         STATION_STRUCTURE_COLOR);

    quad(ring(side, STATION_TOWER_RADIUS, STATION_BASE_TOP), ring(side, STATION_TOWER_RADIUS, STATION_TOWER_TOP),
         ring(side + 1, STATION_TOWER_RADIUS, STATION_TOWER_TOP), ring(side + 1, STATION_TOWER_RADIUS, STATION_BASE_TOP),
         STATION_STRUCTURE_COLOR);

    // The cap, as a fan from the axis. Next facet before this one, so the normal points up.
    triangle(StationPoint{0.0F, STATION_TOWER_TOP, 0.0F}, ring(side + 1, STATION_TOWER_RADIUS, STATION_TOWER_TOP),
             ring(side, STATION_TOWER_RADIUS, STATION_TOWER_TOP), STATION_BEACON_COLOR);
  }

  // Four solar panels, flat and facing up, on the axes. Flat rather than solid for the same
  // reason as the ship's wings: the camera is always above the plane, so the underside is never
  // seen and thickness would buy nothing.
  for (std::size_t quarter = 0; quarter < STATION_PANEL_COUNT; ++quarter)
  {
    const StationPoint& along = STATION_DIRECTIONS[quarter * 2];
    // The direction across the panel is the plan direction two facets on, which for a regular
    // octagon is a quarter turn.
    const StationPoint& across = STATION_DIRECTIONS[(quarter * 2 + 2) % STATION_SIDES];

    const auto corner = [&along, &across](float _distance, float _side)
    { return StationPoint{along.x * _distance + across.x * _side, STATION_PANEL_HEIGHT, along.z * _distance + across.z * _side}; };

    quad(corner(STATION_PANEL_INNER, -STATION_PANEL_HALF_WIDTH), corner(STATION_PANEL_INNER, STATION_PANEL_HALF_WIDTH),
         corner(STATION_PANEL_OUTER, STATION_PANEL_HALF_WIDTH), corner(STATION_PANEL_OUTER, -STATION_PANEL_HALF_WIDTH),
         STATION_PANEL_COLOR);
  }

  return vertices;
}

[[nodiscard]] inline constexpr std::array<std::uint16_t, STATION_VERTEX_COUNT> BuildStationIndices() noexcept
{
  std::array<std::uint16_t, STATION_VERTEX_COUNT> indices = {};
  for (std::size_t vertex = 0; vertex < STATION_VERTEX_COUNT; ++vertex)
  {
    indices[vertex] = static_cast<std::uint16_t>(vertex);
  }
  return indices;
}

inline constexpr std::array<Neuron::MeshVertex, STATION_VERTEX_COUNT> STATION_VERTICES = BuildStationVertices();
inline constexpr std::array<std::uint16_t, STATION_VERTEX_COUNT> STATION_INDICES = BuildStationIndices();

// Two things a generator can get wrong silently, checked at compile time rather than looked for
// on screen: emitting the wrong number of triangles, and a degenerate face -- one whose three
// corners are in a line, which has no normal and shades as whichever tone zero happens to fall on.
static_assert(std::ranges::none_of(STATION_VERTICES, [](const Neuron::MeshVertex& _vertex)
                                   { return _vertex.normalX == 0.0F && _vertex.normalY == 0.0F && _vertex.normalZ == 0.0F; }),
              "A station face is degenerate: its three corners are collinear and it has no normal.");
static_assert(std::ranges::all_of(STATION_VERTICES, [](const Neuron::MeshVertex& _vertex) { return _vertex.paletteIndex < 8; }),
              "A face's authored index is the DARK half of a palette pair and must be 0-7.");

/// Where the station stands.
///
/// Chosen through the projection rather than by eye. The camera puts a world point at
/// ((x - z) * 8, (x + z) * 4) virtual pixels from the ship (ADR-003), so x - z = -26 places the
/// station 208 pixels to the left -- far enough that its 51-pixel half-width clears the ship's 43
/// -- and x + z = 0 puts it level, where its 72-pixel-tall silhouette has room above.
///
/// The distance is deliberately NOT scaled with the objects. Halving both and halving the gap
/// between them would just be the same picture at a different zoom; leaving the world the size it
/// was is what makes them smaller *in it*.
///
/// It is also what makes the ship's movement visible at all. Space is black and the camera
/// follows the ship, so with nothing else in the scene a ship at full speed looks identical to a
/// ship standing still: the only thing that changed was the status line. The station is the fixed
/// thing the ship moves against.
inline constexpr float STATION_POSITION_X = -13.0F;
inline constexpr float STATION_POSITION_Z = 13.0F;

} // namespace Frontier
