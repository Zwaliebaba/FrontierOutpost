#pragma once

namespace Neuron
{

/// A perspective camera that orbits a point, and projects world points to screen pixels.
///
/// It is a real pinhole camera: an eye position, a direction, a field of view. Near things are
/// larger, parallel lines converge, and moving the eye changes what is in front of what -- none of
/// which the authored curve it replaced could do (ADR-017).
///
/// THERE IS NO MATRIX HERE, and that is deliberate rather than an omission. Every point this
/// projects is projected on the CPU, because the interface is drawn as screen-pixel triangles
/// (ADR-014) -- so a 4x4 would be built, multiplied and then immediately used one point at a time.
/// Working in view space directly is the same arithmetic with the row-vector-versus-column
/// question, the depth-range convention and the handedness all removed, and every step of it can
/// be read against the geometry it describes. If a vertex shader ever needs this camera, THAT is
/// when it grows a matrix.
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
