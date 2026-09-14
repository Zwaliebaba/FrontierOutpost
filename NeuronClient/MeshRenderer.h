#pragma once

#include "Color.h"
#include "OrbitCamera.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

/// Lit solids in WORLD units, RECORDED. Drawing them is `MeshBackend`.
///
/// **This is the sibling of `ShapeRenderer`, not an extension of it** (ADR-103). That recorder is
/// two-dimensional interface geometry in canvas pixels, and every page and every test depends on
/// its vertex being two floats and a colour. A mesh is a different thing in every field: it sits in
/// the world, it carries a normal, it carries a ramp of tones for a light to choose between, and it
/// is depth-tested against its own kind. Pressing those onto the shape vertex would have made every
/// rectangle on the screen pay for a normal it does not have.
///
/// The split is the one `ShapeRenderer` already makes: this names no graphics API, a `MeshBackend`
/// drains it and is the only half that knows about D3D12 (ADR-075). Every page and every test talks
/// to this type; a headless test can tessellate a sphere and count its triangles without a device.
///
/// **A vertex carries six authored tones and the GPU picks one per pixel** -- three thresholds on
/// the light choose between the shadow, the band past the terminator, the grazed band and the lit
/// band, a threshold on the view picks the silhouette, and one on the half-vector picks the glint. Never a value that is not one
/// of them (ADR-012's rule, kept while its count was dropped: ADR-104, ADR-105, ADR-106). The
/// normal is what the choices are made from, so it is interpolated; the tones are not, because a
/// tone that interpolated would be exactly the gradient the rule forbids.
///
/// Nothing here is anti-aliased and nothing blends, like everything else in this renderer
/// (ADR-011, ADR-014). A ball's edge is a staircase, and a tone is a tone.
class MeshRenderer
{
public:
  using WorldPoint = OrbitCamera::WorldPoint;

  /// A world position, a unit normal, and the six tones. R8: a public aggregate handed to the
  /// GPU, so plain fields -- and public, because a backend is what hands it over.
  struct MeshVertex
  {
    float x;
    float y;
    float z;
    float nx;
    float ny;
    float nz;
    std::uint32_t litColor;
    std::uint32_t halfLitColor;
    /// The first step out of the shadow. See `ColorRamp::quarterLit` (ADR-108).
    std::uint32_t quarterLitColor;
    std::uint32_t darkColor;
    /// The silhouette, where the surface turns away from the EYE rather than from the light. An
    /// authored tone and not a computed one: the shader selects it exactly as it selects the
    /// others, so a pixel is still a colour somebody named (ADR-104).
    std::uint32_t rimColor;
    /// The glint. See `ColorRamp::glint`.
    std::uint32_t glintColor;
  };

  /// One batch of recorded vertices, and where it sits in this frame's recording. The index is not
  /// bookkeeping, for the reason `ShapeRenderer::Batch` gives: a frame may be drained more than
  /// once, and every batch has to land somewhere the GPU can still read when the list runs.
  struct Batch
  {
    std::span<const MeshVertex> vertices;
    std::uint32_t firstVertex;
  };

  /// How this frame's world reaches the canvas: the camera as a matrix, the light, and the
  /// rectangle of the canvas the camera's clip space lands in.
  ///
  /// It is set by the page that owns the camera, because the recorder cannot know which camera the
  /// triangles were recorded against -- and a mesh recorded in world units is meaningless without
  /// one. The backend reads it when it draws.
  struct View
  {
    /// World to clip, row-major, right-handed, y up, D3D 0..1 depth: `OrbitCamera::ViewProjection`.
    std::array<float, 16> viewProjection = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    /// World space, unit length, pointing TOWARD the light. Fixed to the world and not to the eye,
    /// so orbiting the camera turns the lit side of everything.
    WorldPoint lightDirection = {0.0F, 1.0F, 0.0F};
    /// Where the eye is, in world space. The rim term needs a direction to the viewer per pixel,
    /// which is this minus the pixel's world position -- exact, rather than the view axis, so a
    /// ball at the edge of a wide pane rims on its own silhouette and not on the pane's.
    WorldPoint eyePosition = {0.0F, 0.0F, 1.0F};
    /// The camera's viewport in canvas pixels: where clip space (-1..1) lands. The camera projects
    /// into a pane rather than the whole canvas, and the backend draws into the same pane.
    float viewportXPixels = 0.0F;
    float viewportYPixels = 0.0F;
    float viewportWidthPixels = 1.0F;
    float viewportHeightPixels = 1.0F;
  };

  /// One frame's worth of geometry. Sized for a whole galaxy seen at once: a twelve-player match is
  /// forty-nine systems, and a station is at most 504 vertices of ball plus 36 of column, so this
  /// holds sixty of them. Overrunning it is a broken invariant rather than a case to grow into
  /// (Debug.h).
  static constexpr std::uint32_t MAX_VERTICES_PER_FRAME = 32768;

  /// How many segments a sphere is drawn with around its equator, by its radius on the screen. A
  /// far ball is four pixels across and eight segments already lose nothing a pixel can show; a
  /// near one at twelve is a twelve-gon whose chord error is under a pixel at any radius the map's
  /// zoom can reach. Rings follow at two thirds of the segments (`Sphere`).
  [[nodiscard]] static constexpr std::uint32_t SegmentsForRadius(float _radiusPixels) noexcept
  {
    const auto wanted = static_cast<std::uint32_t>(_radiusPixels * 2.0F);
    return std::clamp(wanted, MIN_SPHERE_SEGMENTS, MAX_SPHERE_SEGMENTS);
  }

  /// How many latitude bands a sphere of `_segments` gets. Two thirds, so a 12-segment ball is
  /// 12x8 and its bands are about as tall as its segments are wide.
  [[nodiscard]] static constexpr std::uint32_t RingsForSegments(std::uint32_t _segments) noexcept
  {
    return std::max(MIN_SPHERE_RINGS, (_segments * 2) / 3);
  }

  /// How many vertices `Sphere` records for `_segments`, so a caller can budget. The two polar
  /// bands are triangles and every other band is a quad.
  [[nodiscard]] static constexpr std::uint32_t SphereVertexCount(std::uint32_t _segments) noexcept
  {
    return _segments * (2 * RingsForSegments(_segments) - 2) * 3;
  }

  /// What `Column` records: four sides of two triangles, and a cap.
  static constexpr std::uint32_t COLUMN_VERTEX_COUNT = 5 * 2 * 3;

  /// Starts a frame's recording over. Not noexcept, for the reason `ShapeRenderer::BeginFrame` is
  /// not: the first call reserves the vector.
  void BeginFrame();

  void SetView(const View& _view) noexcept
  {
    m_view = _view;
  }
  [[nodiscard]] const View& CurrentView() const noexcept
  {
    return m_view;
  }

  /// A UV sphere: `_segments` around, `RingsForSegments(_segments)` from pole to pole, with a
  /// smooth outward normal at every vertex so the bands the shader draws are curves rather than
  /// sets of facets.
  void Sphere(const WorldPoint& _center, float _radius, const ColorRamp& _tones, std::uint32_t _segments);

  /// A square column standing on the ground: from `_foot` up by `_height`, `_halfWidth` to a side,
  /// with a cap on top. Every vertex of a face carries the face's own normal, so the light chooses
  /// one tone for the whole face -- which is what makes a stem read as a solid with a lit side and
  /// a shaded one rather than as a scratch (ADR-105).
  ///
  /// Four sides and a cap, and no floor: a column stands ON the plane, so its underside is never
  /// visible and two triangles of it would be two triangles the culler throws away.
  void Column(const WorldPoint& _foot, float _height, float _halfWidth, const ColorRamp& _tones);

  /// Eight faces, flat: every vertex of a face carries the face's own normal, so the light chooses
  /// one tone per face exactly as ADR-012 had it.
  ///
  /// **It is DIRECTED, which is what lets it replace an arrowhead** (ADR-106). `_forward` is the way
  /// it points, in the ground plane; `_halfLength` is its reach along that, `_halfWidth` across it
  /// and `_halfHeight` up. A fleet marker has to say which way the fleet is going, so a symmetric
  /// solid would have thrown away the one thing the flat triangle it replaces was carrying. Equal
  /// length and width give the symmetric octahedron back.
  void Octahedron(const WorldPoint& _center, const WorldPoint& _forward, float _halfLength, float _halfWidth, float _halfHeight,
                  const ColorRamp& _tones);

  /// Everything recorded since the last take, and marks it taken. Empty when nothing is new. The
  /// span points into this recorder and stays valid until the next `BeginFrame`.
  [[nodiscard]] Batch TakeUnflushed() noexcept;

  /// The recording so far, for a test that wants to look at the triangles rather than count them.
  [[nodiscard]] std::span<const MeshVertex> Vertices() const noexcept
  {
    return m_vertices;
  }

private:
  static constexpr std::uint32_t MIN_SPHERE_SEGMENTS = 8;
  static constexpr std::uint32_t MAX_SPHERE_SEGMENTS = 12;
  static constexpr std::uint32_t MIN_SPHERE_RINGS = 4;

  /// The one place vertices are appended, and the one overrun check. Wound COUNTER-CLOCKWISE seen
  /// from outside -- the right-handed convention, which is what `MeshBackend`'s rasterizer is told
  /// a front face is -- so a caller that gets the order wrong records a solid the culler removes.
  void AppendTriangle(const MeshVertex& _a, const MeshVertex& _b, const MeshVertex& _c);

  /// A flat quad, corners counter-clockwise from outside, with one normal on all four corners.
  void AppendFlatQuad(const WorldPoint& _a, const WorldPoint& _b, const WorldPoint& _c, const WorldPoint& _d, const WorldPoint& _normal,
                      const ColorRamp& _tones);

  std::vector<MeshVertex> m_vertices;
  std::size_t m_takenThisFrame = 0;
  View m_view;
};

} // namespace Neuron
