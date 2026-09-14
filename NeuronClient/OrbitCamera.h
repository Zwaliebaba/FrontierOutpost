#pragma once

#include <array>

namespace Neuron
{

/// A perspective camera that orbits a point, and projects world points to screen pixels.
///
/// It is a real pinhole camera: an eye position, a direction, a field of view. Near things are
/// larger, parallel lines converge, and moving the eye changes what is in front of what -- none of
/// which the authored curve it replaced could do (ADR-017).
///
/// THERE IS ONE MATRIX HERE, AND IT IS A SECOND STATEMENT OF THE SAME CAMERA. Every point the
/// interface draws is projected on the CPU by `Project`, because the interface is drawn as
/// canvas-pixel triangles (ADR-014) and a 4x4 would be built, multiplied and then used one point at
/// a time; working in view space directly is the same arithmetic with the row-vector-versus-column
/// question, the depth-range convention and the handedness all removed, and every step of it can be
/// read against the geometry it describes. The mesh pass is different in kind: its vertices are
/// transformed on the GPU, so it takes the camera as `ViewProjection()` -- built from the same
/// basis, eye, field of view and viewport, and held to `Project` by a test that pushes points
/// through both and asks for the same pixel (ADR-103). Neither is derived from the other; they are
/// kept equal, which is the only way two statements of one thing stay one thing.
///
/// Right-handed, y up. Depth is distance along the view direction, positive in front of the eye.
class OrbitCamera
{
public:
  struct WorldPoint
  {
    float x;
    float y;
    float z;
  };

  struct ScreenPoint
  {
    float xPixels;
    float yPixels;
    /// Distance in front of the eye, in world units. What back-to-front sorting is done on: with
    /// an orbiting camera the draw order is no longer a property of the scene (ADR-017).
    float depth;
    /// False when the point is behind the eye or on the plane through it, where a perspective
    /// divide has no answer. A caller that draws anyway gets a point mirrored through the eye.
    bool visible;
  };

  /// How far the eye may rise and fall. Not preferences -- both ends are places where the picture
  /// stops working. At the bottom the camera is in the plane and the whole galaxy collapses to a
  /// line; at the top it is directly overhead, the stems point at the eye and every system is a
  /// dot on its own shadow.
  static constexpr float MIN_PITCH_RADIANS = 0.12F;
  static constexpr float MAX_PITCH_RADIANS = 1.40F;

  /// Vertical field of view. Narrow-ish: a wide lens on a map this size bows the near lanes
  /// outwards and makes the far half tiny, and the authored view this replaces was not that
  /// extreme (ADR-017).
  static constexpr float DEFAULT_FIELD_OF_VIEW_RADIANS = 0.70F;

  /// Where `ViewProjection`'s depth range starts and ends, in world units in front of the eye. The
  /// near plane is one unit: the pitch clamp keeps the eye above the plane and the framing keeps it
  /// outside the galaxy, so nothing drawn is ever that close. The far plane is a multiple of the
  /// orbit distance rather than a constant, because the distance is solved from the content and a
  /// galaxy the camera has framed lies within a couple of distances of the target at any zoom.
  /// `Project` has no such range -- it divides by whatever depth it is given -- so a point beyond
  /// the far plane is one the CPU draws and the GPU clips, and the margin here is what keeps that
  /// from happening.
  static constexpr float NEAR_PLANE_WORLD_UNITS = 1.0F;
  static constexpr float FAR_PLANE_IN_DISTANCES = 8.0F;

  /// Where the picture is drawn, in screen pixels. The aspect comes from this, so a camera with
  /// no viewport set projects nothing useful.
  void SetViewport(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels) noexcept;

  void SetTarget(const WorldPoint& _target) noexcept
  {
    m_target = _target;
  }
  void SetDistance(float _distance) noexcept
  {
    m_distance = std::max(_distance, 1.0F);
  }
  void SetFieldOfViewRadians(float _fieldOfView) noexcept
  {
    m_fieldOfViewRadians = std::clamp(_fieldOfView, 0.1F, 2.8F);
  }

  /// Where the camera sits and where it looks. Yaw is free and wraps; pitch is clamped.
  void SetOrientation(float _yawRadians, float _pitchRadians) noexcept;

  /// Adds to the current orientation. What a drag calls.
  void Orbit(float _yawDeltaRadians, float _pitchDeltaRadians) noexcept
  {
    SetOrientation(m_yawRadians + _yawDeltaRadians, m_pitchRadians + _pitchDeltaRadians);
  }

  [[nodiscard]] float YawRadians() const noexcept
  {
    return m_yawRadians;
  }
  [[nodiscard]] float PitchRadians() const noexcept
  {
    return m_pitchRadians;
  }
  [[nodiscard]] float Distance() const noexcept
  {
    return m_distance;
  }

  /// The eye, in world space.
  [[nodiscard]] WorldPoint Position() const noexcept;

  [[nodiscard]] ScreenPoint Project(const WorldPoint& _world) const noexcept;

  /// The same camera as a 4x4, for a vertex shader: world to clip, ROW-MAJOR with the point as a
  /// row vector on the left (`mul(float4(p, 1), m)` in HLSL against a `row_major` matrix),
  /// right-handed, y up, Direct3D's 0..1 depth with `NEAR_PLANE_WORLD_UNITS` at 0 and
  /// `FAR_PLANE_IN_DISTANCES` times the orbit distance at 1.
  ///
  /// Clip space is the VIEWPORT's: x and y in -1..1 span the rectangle `SetViewport` gave, not the
  /// canvas, so a pass that draws through this sets its viewport to the same rectangle. A point at
  /// `(x, y)` after the homogeneous divide lands on the pixel `Project` would have given it, which
  /// `NeuronClientTests` holds to a hundredth of a pixel.
  [[nodiscard]] std::array<float, 16> ViewProjection() const noexcept;

  /// Projects a DIRECTION rather than a place: where something infinitely far away in `_direction`
  /// lands on the screen.
  ///
  /// **This is not `Project` of a point a long way off, and the difference is the whole reason it
  /// exists.** A point at a distance has parallax -- move the eye and it shifts -- and the only way
  /// to make that shift vanish is to pick a distance so large the arithmetic drops it, which is a
  /// fudge factor whose right value depends on the scene. A direction has no position to have
  /// parallax with: the eye cancels out of the subtraction before it is ever done, so this is the
  /// exact answer for infinity rather than a close approximation of it.
  ///
  /// The returned `depth` is the cosine of the angle from the view axis, not a distance. It is
  /// positive in front of the eye, which is what `visible` is decided on; sorting distant things
  /// against each other by it is meaningless and nothing should try.
  [[nodiscard]] ScreenPoint ProjectDirection(const WorldPoint& _direction) const noexcept;

  /// Whether a projected point lands inside the rectangle this camera draws into.
  ///
  /// Tests the CENTRE only, so a dot straddling the edge is either drawn whole or not at all. For
  /// the sky, whose stars are a pixel across and whose renderer does not clip, that is at most a
  /// pixel of error at the boundary; anything larger wants a clip rectangle rather than this.
  [[nodiscard]] bool InsideViewport(const ScreenPoint& _point) const noexcept;

  /// How many screen pixels one world unit spans at a given depth. What node radii, stem heights
  /// and ground circles are sized with, so that a system genuinely gets larger as it comes
  /// nearer rather than being scaled by a rule about where it is.
  [[nodiscard]] float PixelsPerWorldUnitAt(float _depth) const noexcept;

private:
  /// The three view axes, recomputed on demand from yaw and pitch. Cheap enough that caching them
  /// would be machinery in front of two sines.
  struct Basis
  {
    WorldPoint right;
    WorldPoint up;
    WorldPoint forward;
  };

  [[nodiscard]] Basis ViewBasis() const noexcept;

  WorldPoint m_target = {0.0F, 0.0F, 0.0F};
  float m_yawRadians = 0.0F;
  float m_pitchRadians = 0.6F;
  float m_distance = 100.0F;
  float m_fieldOfViewRadians = DEFAULT_FIELD_OF_VIEW_RADIANS;

  float m_viewportXPixels = 0.0F;
  float m_viewportYPixels = 0.0F;
  float m_viewportWidthPixels = 1.0F;
  float m_viewportHeightPixels = 1.0F;
};

} // namespace Neuron
