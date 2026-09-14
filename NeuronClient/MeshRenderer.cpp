// MeshRenderer.cpp -- spheres, columns and octahedra, tessellated on the CPU into one world-space
// triangle list. See MeshRenderer.h for why this is a second recorder and not a third field on the
// shape vertex, and MeshBackend for how the list reaches a GPU.

#include "pch.h"
#include "MeshRenderer.h"

namespace Neuron
{

namespace
{

constexpr float PI = 3.14159265358979323846F;
constexpr float TWO_PI = 6.28318530717958647692F;

} // namespace

void MeshRenderer::BeginFrame()
{
  // Cleared, not freed, and reserved on the first frame only: capacity survives clear(), so this
  // is a no-op from the second frame onwards and no frame's recording allocates.
  m_vertices.clear();
  m_vertices.reserve(MAX_VERTICES_PER_FRAME);
  m_takenThisFrame = 0;
}

void MeshRenderer::AppendTriangle(const MeshVertex& _a, const MeshVertex& _b, const MeshVertex& _c)
{
  ASSERT_TEXT(m_vertices.size() + 3 <= MAX_VERTICES_PER_FRAME,
              L"More mesh geometry in one frame than MeshRenderer::MAX_VERTICES_PER_FRAME allows.");

  m_vertices.push_back(_a);
  m_vertices.push_back(_b);
  m_vertices.push_back(_c);
}

void MeshRenderer::AppendFlatQuad(const WorldPoint& _a, const WorldPoint& _b, const WorldPoint& _c, const WorldPoint& _d,
                                  const WorldPoint& _normal, const ColorRamp& _tones)
{
  const std::uint32_t lit = Pack(_tones.lit);
  const std::uint32_t halfLit = Pack(_tones.halfLit);
  const std::uint32_t dark = Pack(_tones.shaded);
  const std::uint32_t rim = Pack(_tones.rim);

  const auto corner = [&](const WorldPoint& _at)
  { return MeshVertex{_at.x, _at.y, _at.z, _normal.x, _normal.y, _normal.z, lit, halfLit, dark, rim}; };

  AppendTriangle(corner(_a), corner(_b), corner(_c));
  AppendTriangle(corner(_a), corner(_c), corner(_d));
}

void MeshRenderer::Sphere(const WorldPoint& _center, float _radius, const ColorRamp& _tones, std::uint32_t _segments)
{
  if (_radius <= 0.0F)
  {
    return;
  }

  const std::uint32_t segments = std::clamp(_segments, MIN_SPHERE_SEGMENTS, MAX_SPHERE_SEGMENTS);
  const std::uint32_t rings = RingsForSegments(segments);
  const std::uint32_t lit = Pack(_tones.lit);
  const std::uint32_t halfLit = Pack(_tones.halfLit);
  const std::uint32_t dark = Pack(_tones.shaded);
  const std::uint32_t rim = Pack(_tones.rim);

  // Latitude runs from the +y pole (ring 0) down to the -y pole; longitude runs from +x toward
  // +z. The normal at a point on a sphere is the direction from its centre, which is the one thing
  // that makes a UV sphere cheap to light: the position IS the normal, scaled.
  const auto at = [&](std::uint32_t _segment, std::uint32_t _ring)
  {
    const float latitude = PI * static_cast<float>(_ring) / static_cast<float>(rings);
    const float longitude = TWO_PI * static_cast<float>(_segment % segments) / static_cast<float>(segments);
    const float nx = std::sin(latitude) * std::cos(longitude);
    const float ny = std::cos(latitude);
    const float nz = std::sin(latitude) * std::sin(longitude);
    return MeshVertex{_center.x + nx * _radius, _center.y + ny * _radius, _center.z + nz * _radius, nx, ny, nz, lit, halfLit, dark, rim};
  };

  for (std::uint32_t ring = 0; ring < rings; ++ring)
  {
    for (std::uint32_t segment = 0; segment < segments; ++segment)
    {
      // The band's four corners. Longitude increases toward +z, which from outside the +x side of
      // the ball is leftward, and the ring below is downward -- so this order runs the top edge
      // left, then down, then the bottom edge right: counter-clockwise seen from outside.
      const MeshVertex a = at(segment, ring);
      const MeshVertex b = at(segment + 1, ring);
      const MeshVertex c = at(segment + 1, ring + 1);
      const MeshVertex d = at(segment, ring + 1);

      // The polar bands collapse one edge to the pole and are one triangle each; a quad there
      // would be a triangle and a sliver of nothing.
      if (ring != 0)
      {
        AppendTriangle(a, b, c);
      }
      if (ring + 1 != rings)
      {
        AppendTriangle(a, c, d);
      }
    }
  }
}

void MeshRenderer::Column(const WorldPoint& _foot, float _height, float _halfWidth, const ColorRamp& _tones)
{
  if (_height <= 0.0F || _halfWidth <= 0.0F)
  {
    return;
  }

  const float low = _foot.y;
  const float high = _foot.y + _height;

  // **THE CROSS-SECTION IS A DIAMOND, NOT A SQUARE, AND THAT IS THE WHOLE POINT** (ADR-105). A
  // square column standing on an axis-aligned plane presents ONE face to a camera at the default
  // yaw, so it lands in one band and reads as a flat bar -- measured that way on the first build,
  // three pixels of a single tone. Turned forty-five degrees it presents TWO, at different angles
  // to the light, so the near edge is lit and the far one is shadow and the stem reads as a solid
  // from the moment the map opens.
  //
  // The corner order is `Octahedron`'s rim order, +x, -z, -x, +z, and for its reason.
  const std::array<WorldPoint, 4> corners = {
    WorldPoint{_foot.x + _halfWidth, 0.0F, _foot.z},
    WorldPoint{_foot.x, 0.0F, _foot.z - _halfWidth},
    WorldPoint{_foot.x - _halfWidth, 0.0F, _foot.z},
    WorldPoint{_foot.x, 0.0F, _foot.z + _halfWidth},
  };

  for (std::size_t index = 0; index < corners.size(); ++index)
  {
    const WorldPoint& here = corners[index];
    const WorldPoint& next = corners[(index + 1) % corners.size()];

    // A side's outward normal is the direction from the axis to the middle of its two corners,
    // which for a diamond is the same arithmetic for every face.
    const float midX = (here.x + next.x) * 0.5F - _foot.x;
    const float midZ = (here.z + next.z) * 0.5F - _foot.z;
    const float reach = std::sqrt(midX * midX + midZ * midZ);
    const WorldPoint outward = reach > 0.0F ? WorldPoint{midX / reach, 0.0F, midZ / reach} : WorldPoint{1.0F, 0.0F, 0.0F};

    AppendFlatQuad(WorldPoint{here.x, low, here.z}, WorldPoint{next.x, low, next.z}, WorldPoint{next.x, high, next.z},
                   WorldPoint{here.x, high, here.z}, outward, _tones);
  }

  // The cap, in the same corner order, which comes out facing +y.
  AppendFlatQuad(WorldPoint{corners[0].x, high, corners[0].z}, WorldPoint{corners[1].x, high, corners[1].z},
                 WorldPoint{corners[2].x, high, corners[2].z}, WorldPoint{corners[3].x, high, corners[3].z}, WorldPoint{0.0F, 1.0F, 0.0F},
                 _tones);
}

void MeshRenderer::Octahedron(const WorldPoint& _center, float _halfWidth, float _halfHeight, const ColorRamp& _tones)
{
  if (_halfWidth <= 0.0F || _halfHeight <= 0.0F)
  {
    return;
  }

  const std::uint32_t lit = Pack(_tones.lit);
  const std::uint32_t halfLit = Pack(_tones.halfLit);
  const std::uint32_t dark = Pack(_tones.shaded);
  const std::uint32_t rimTone = Pack(_tones.rim);

  const WorldPoint top = {_center.x, _center.y + _halfHeight, _center.z};
  const WorldPoint bottom = {_center.x, _center.y - _halfHeight, _center.z};
  // The equator, counter-clockwise seen from above (+y): +x, -z, -x, +z. Right-handed with y up
  // means +z is toward a viewer standing at +z, so from above it runs the OTHER way round the
  // circle than it does around the sphere's longitude, which starts at +x and turns toward +z.
  const std::array<WorldPoint, 4> rim = {
    WorldPoint{_center.x + _halfWidth, _center.y, _center.z},
    WorldPoint{_center.x, _center.y, _center.z - _halfWidth},
    WorldPoint{_center.x - _halfWidth, _center.y, _center.z},
    WorldPoint{_center.x, _center.y, _center.z + _halfWidth},
  };

  // A flat face: the normal is the face's own, put on all three corners, so the shader has nothing
  // to interpolate and the whole face lands in one band (ADR-012).
  const auto face = [&](const WorldPoint& _a, const WorldPoint& _b, const WorldPoint& _c)
  {
    const float ux = _b.x - _a.x;
    const float uy = _b.y - _a.y;
    const float uz = _b.z - _a.z;
    const float vx = _c.x - _a.x;
    const float vy = _c.y - _a.y;
    const float vz = _c.z - _a.z;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length > 0.0F)
    {
      nx /= length;
      ny /= length;
      nz /= length;
    }
    AppendTriangle(MeshVertex{_a.x, _a.y, _a.z, nx, ny, nz, lit, halfLit, dark, rimTone},
                   MeshVertex{_b.x, _b.y, _b.z, nx, ny, nz, lit, halfLit, dark, rimTone},
                   MeshVertex{_c.x, _c.y, _c.z, nx, ny, nz, lit, halfLit, dark, rimTone});
  };

  for (std::size_t index = 0; index < rim.size(); ++index)
  {
    // `here`/`next`, not `near`/`far`: both of those are empty macros in `minwindef.h`.
    const WorldPoint& here = rim[index];
    const WorldPoint& next = rim[(index + 1) % rim.size()];
    // Upper faces run rim -> next rim -> apex; lower ones the other way round, so both are
    // counter-clockwise from outside.
    face(here, next, top);
    face(next, here, bottom);
  }
}

MeshRenderer::Batch MeshRenderer::TakeUnflushed() noexcept
{
  const Batch batch = {
    .vertices = std::span{m_vertices}.subspan(m_takenThisFrame),
    .firstVertex = static_cast<std::uint32_t>(m_takenThisFrame),
  };
  m_takenThisFrame = m_vertices.size();
  return batch;
}

} // namespace Neuron
