#pragma once

#include "MeshRenderer.h"

namespace Frontier
{

// The ship, as twenty-four triangles, embedded in the binary (R13).
//
// This is GAME content and it is deliberately not in NeuronClient (R9): the engine draws meshes
// and has no idea what a ship is. What crosses the boundary is Neuron::MeshVertex and nothing
// ship-shaped.
//
// The hull points along +X at heading zero and sits on y = 0. One unit is one metre. The table
// below is drawn at twice the final size and SHIP_SCALE halves it, so the ship is 10.75 m from
// nose (x = 12) to exhaust (x = -9.5) and 9.5 m across the wings (z = -/+9.5). At the camera's 8
// pixels a ground unit that is 86 virtual pixels of a 640-pixel screen (ADR-003).
//
// It is authored as FACES rather than as vertices, and the vertex array below is computed from
// them at compile time. That is not tidiness: flat shading needs a normal per face and no shared
// vertices, so hand-writing the vertex array would mean seventy-two entries with seventy-two
// normals in them, and a normal that disagrees with its triangle is a shading bug nobody can see
// the cause of.

/// A triangle as authored: three corners, wound so that the cross product of the first two edges
/// points OUT of the hull, and the dark half of its palette pair.
struct ShipFace
{
  float ax;
  float ay;
  float az;
  float bx;
  float by;
  float bz;
  float cx;
  float cy;
  float cz;
  /// 0-7. The light adds 8 to pick the bright variant of the same hue (ADR-002), so a base index
  /// of 8 or more would run off the end of the palette.
  std::uint32_t paletteIndex;
};

// The palette pairs the ship is built from. Hull is gray/white, wings blue, engines red -- three
// hues is as many as a silhouette this size can carry before it stops reading as one object.
inline constexpr std::uint32_t HULL_COLOR = 7;   // AAAAAA, lighting to FFFFFF
inline constexpr std::uint32_t WING_COLOR = 1;   // 0000AA, lighting to 5555FF
inline constexpr std::uint32_t ENGINE_COLOR = 4; // AA0000, lighting to FF5555

// The hull is a raised DECK with flanks falling away to the beam, rather than a simple faceted
// spindle. That shape is the reason the ship reads as a solid: the deck and the nose and tail
// slopes all face broadly up, the six flanks face broadly sideways, and a light raked in from the
// port quarter therefore puts the deck in the bright tone and every flank in the dark one
// (ADR-002). A hull whose faces all tilt upwards -- which the first attempt at this mesh was --
// comes out uniformly bright from any light above it, and looks like a paper aeroplane.
//
//   deck corners  TFP/TFS (5, 2.2, -/+1.2)      TBP/TBS (-4, 2.2, -/+1.2)
//   nose  N (12, 0.4, 0)      tail  T (-8, 0.4, 0)      beam  P/S (0.5, 0, -/+3.5)
inline constexpr std::array<ShipFace, 24> SHIP_FACES = {{
  // Deck, two triangles, facing straight up.
  {5.0F, 2.2F, -1.2F, -4.0F, 2.2F, -1.2F, -4.0F, 2.2F, 1.2F, HULL_COLOR},
  {5.0F, 2.2F, -1.2F, -4.0F, 2.2F, 1.2F, 5.0F, 2.2F, 1.2F, HULL_COLOR},

  // The slopes down from the deck to the nose and to the tail.
  {5.0F, 2.2F, -1.2F, 5.0F, 2.2F, 1.2F, 12.0F, 0.4F, 0.0F, HULL_COLOR},
  {-4.0F, 2.2F, 1.2F, -4.0F, 2.2F, -1.2F, -8.0F, 0.4F, 0.0F, HULL_COLOR},

  // Starboard flanks: forward, midships, aft.
  {5.0F, 2.2F, 1.2F, 0.5F, 0.0F, 3.5F, 12.0F, 0.4F, 0.0F, HULL_COLOR},
  {5.0F, 2.2F, 1.2F, -4.0F, 2.2F, 1.2F, 0.5F, 0.0F, 3.5F, HULL_COLOR},
  {-4.0F, 2.2F, 1.2F, -8.0F, 0.4F, 0.0F, 0.5F, 0.0F, 3.5F, HULL_COLOR},

  // Port flanks, mirrored in Z with the winding reversed so the normals still point outwards.
  {5.0F, 2.2F, -1.2F, 12.0F, 0.4F, 0.0F, 0.5F, 0.0F, -3.5F, HULL_COLOR},
  {5.0F, 2.2F, -1.2F, 0.5F, 0.0F, -3.5F, -4.0F, 2.2F, -1.2F, HULL_COLOR},
  {-4.0F, 2.2F, -1.2F, 0.5F, 0.0F, -3.5F, -8.0F, 0.4F, 0.0F, HULL_COLOR},

  // The belly, facing down. Never seen from a camera that is always above the plane, and here so
  // that the hull is a closed solid rather than a shell with a hole in it.
  {12.0F, 0.4F, 0.0F, -8.0F, 0.4F, 0.0F, 0.5F, 0.0F, -3.5F, HULL_COLOR},
  {12.0F, 0.4F, 0.0F, 0.5F, 0.0F, 3.5F, -8.0F, 0.4F, 0.0F, HULL_COLOR},

  // The wings: flat, just above the belly line, normals straight up. The camera is always above
  // the plane, so the underside is never seen and giving them thickness would buy nothing. Their
  // roots run inside the hull, which is what makes them look attached rather than balanced there.
  {3.0F, 0.15F, 1.6F, -3.0F, 0.15F, 1.6F, -6.5F, 0.15F, 9.5F, WING_COLOR},
  {3.0F, 0.15F, 1.6F, -6.5F, 0.15F, 9.5F, -0.5F, 0.15F, 7.5F, WING_COLOR},
  {3.0F, 0.15F, -1.6F, -6.5F, 0.15F, -9.5F, -3.0F, 0.15F, -1.6F, WING_COLOR},
  {3.0F, 0.15F, -1.6F, -0.5F, 0.15F, -7.5F, -6.5F, 0.15F, -9.5F, WING_COLOR},

  // Two engine pods, each a tetrahedron tapering to an exhaust point behind the tail.
  {-4.5F, 0.9F, 1.5F, -4.5F, 0.1F, 3.2F, -4.5F, -0.7F, 1.5F, ENGINE_COLOR},
  {-4.5F, 0.9F, 1.5F, -4.5F, -0.7F, 1.5F, -9.5F, 0.1F, 2.3F, ENGINE_COLOR},
  {-4.5F, -0.7F, 1.5F, -4.5F, 0.1F, 3.2F, -9.5F, 0.1F, 2.3F, ENGINE_COLOR},
  {-4.5F, 0.1F, 3.2F, -4.5F, 0.9F, 1.5F, -9.5F, 0.1F, 2.3F, ENGINE_COLOR},

  {-4.5F, 0.9F, -1.5F, -4.5F, -0.7F, -1.5F, -4.5F, 0.1F, -3.2F, ENGINE_COLOR},
  {-4.5F, 0.9F, -1.5F, -9.5F, 0.1F, -2.3F, -4.5F, -0.7F, -1.5F, ENGINE_COLOR},
  {-4.5F, -0.7F, -1.5F, -9.5F, 0.1F, -2.3F, -4.5F, 0.1F, -3.2F, ENGINE_COLOR},
  {-4.5F, 0.1F, -3.2F, -9.5F, 0.1F, -2.3F, -4.5F, 0.9F, -1.5F, ENGINE_COLOR},
}};

inline constexpr std::size_t SHIP_VERTEX_COUNT = SHIP_FACES.size() * 3;

/// What the table above is multiplied by on its way into the vertex buffer.
///
/// The hull is sculpted rather than parameterized -- 24 faces of literal coordinates -- so
/// resizing it by editing the table would mean retyping 216 numbers and getting one wrong. This
/// is the one number instead. The table stays at the proportions it was drawn at and this says
/// how big the ship actually is.
inline constexpr float SHIP_SCALE = 0.5F;

/// Faces to vertices, at compile time: three vertices a face, no sharing, each carrying the face
/// normal. The normal is left unnormalized on purpose -- a cross product is arithmetic a
/// constexpr can do and a square root is not, and MeshVS normalizes anyway (MeshRenderer.h).
///
/// The scale is applied to the corners before the cross product, which costs nothing and keeps
/// the normal honest about the geometry that is actually in the buffer. A uniform scale would not
/// change its direction either way, but "the normal belongs to these three points" is a property
/// worth not having to reason about.
[[nodiscard]] inline constexpr std::array<Neuron::MeshVertex, SHIP_VERTEX_COUNT> BuildShipVertices() noexcept
{
  std::array<Neuron::MeshVertex, SHIP_VERTEX_COUNT> vertices = {};

  for (std::size_t face = 0; face < SHIP_FACES.size(); ++face)
  {
    const ShipFace& source = SHIP_FACES[face];

    const float ax = source.ax * SHIP_SCALE;
    const float ay = source.ay * SHIP_SCALE;
    const float az = source.az * SHIP_SCALE;
    const float bx = source.bx * SHIP_SCALE;
    const float by = source.by * SHIP_SCALE;
    const float bz = source.bz * SHIP_SCALE;
    const float cx = source.cx * SHIP_SCALE;
    const float cy = source.cy * SHIP_SCALE;
    const float cz = source.cz * SHIP_SCALE;

    const float edge1X = bx - ax;
    const float edge1Y = by - ay;
    const float edge1Z = bz - az;
    const float edge2X = cx - ax;
    const float edge2Y = cy - ay;
    const float edge2Z = cz - az;

    const float normalX = edge1Y * edge2Z - edge1Z * edge2Y;
    const float normalY = edge1Z * edge2X - edge1X * edge2Z;
    const float normalZ = edge1X * edge2Y - edge1Y * edge2X;

    vertices[face * 3 + 0] = {ax, ay, az, normalX, normalY, normalZ, source.paletteIndex};
    vertices[face * 3 + 1] = {bx, by, bz, normalX, normalY, normalZ, source.paletteIndex};
    vertices[face * 3 + 2] = {cx, cy, cz, normalX, normalY, normalZ, source.paletteIndex};
  }

  return vertices;
}

[[nodiscard]] inline constexpr std::array<std::uint16_t, SHIP_VERTEX_COUNT> BuildShipIndices() noexcept
{
  std::array<std::uint16_t, SHIP_VERTEX_COUNT> indices = {};
  for (std::size_t vertex = 0; vertex < SHIP_VERTEX_COUNT; ++vertex)
  {
    indices[vertex] = static_cast<std::uint16_t>(vertex);
  }
  return indices;
}

// Flat shading means no vertex is shared, so the index buffer is 0, 1, 2, ... and buys nothing
// today. It is here because MeshRenderer takes indexed geometry and the first mesh that is not
// flat-shaded will need it; a renderer that only draws unindexed meshes is one that has to change
// the day scenery arrives.
inline constexpr std::array<Neuron::MeshVertex, SHIP_VERTEX_COUNT> SHIP_VERTICES = BuildShipVertices();
inline constexpr std::array<std::uint16_t, SHIP_VERTEX_COUNT> SHIP_INDICES = BuildShipIndices();

// ADR-002 shades a face between palette index n and n+8, so an authored index of 8 or more runs
// off the end of the palette. Checking it here makes a bad index a build error rather than a
// wrong color that only shows up when that face happens to catch the light.
static_assert(std::ranges::all_of(SHIP_FACES, [](const ShipFace& _face) { return _face.paletteIndex < 8; }),
              "A face's authored index is the DARK half of a palette pair and must be 0-7.");

} // namespace Frontier
