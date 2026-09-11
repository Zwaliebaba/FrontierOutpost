// OrbitCamera.cpp -- an eye on a sphere around a target, and the perspective divide.
//
// See OrbitCamera.h for why this works in view space rather than building a matrix.

#include "pch.h"
#include "OrbitCamera.h"

namespace Neuron
{

namespace
{

using WorldPoint = OrbitCamera::WorldPoint;

[[nodiscard]] float Dot(const WorldPoint& _a, const WorldPoint& _b) noexcept
{
  return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z;
}

[[nodiscard]] WorldPoint Cross(const WorldPoint& _a, const WorldPoint& _b) noexcept
{
  return WorldPoint{_a.y * _b.z - _a.z * _b.y, _a.z * _b.x - _a.x * _b.z, _a.x * _b.y - _a.y * _b.x};
}

[[nodiscard]] WorldPoint Normalized(const WorldPoint& _v) noexcept
{
  const float length = std::sqrt(Dot(_v, _v));
  if (length <= 0.0F)
  {
    return WorldPoint{0.0F, 0.0F, 1.0F};
  }
  return WorldPoint{_v.x / length, _v.y / length, _v.z / length};
}

/// A point behind the eye, or on the plane through it, has no projection. Everything nearer than
/// this is treated as invisible rather than divided by -- a perspective divide by a number this
/// small throws the point off the screen anyway, and by a negative one puts it on the wrong side.
constexpr float MINIMUM_DEPTH = 0.01F;

} // namespace

void OrbitCamera::SetViewport(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels) noexcept
{
  m_viewportXPixels = _xPixels;
  m_viewportYPixels = _yPixels;
  m_viewportWidthPixels = std::max(_widthPixels, 1.0F);
  m_viewportHeightPixels = std::max(_heightPixels, 1.0F);
}

void OrbitCamera::SetOrientation(float _yawRadians, float _pitchRadians) noexcept
{
  // Yaw is free and wraps; a player who spins the map three times round has yawed three turns and
  // the sines do not care. Pitch is clamped, because both ends of it are degenerate views.
  m_yawRadians = _yawRadians;
  m_pitchRadians = std::clamp(_pitchRadians, MIN_PITCH_RADIANS, MAX_PITCH_RADIANS);
}

OrbitCamera::WorldPoint OrbitCamera::Position() const noexcept
{
  const float cosPitch = std::cos(m_pitchRadians);
  return WorldPoint{
    m_target.x + m_distance * cosPitch * std::sin(m_yawRadians),
    m_target.y + m_distance * std::sin(m_pitchRadians),
    m_target.z + m_distance * cosPitch * std::cos(m_yawRadians),
  };
}

OrbitCamera::Basis OrbitCamera::ViewBasis() const noexcept
{
  const WorldPoint eye = Position();
  const WorldPoint forward = Normalized(WorldPoint{m_target.x - eye.x, m_target.y - eye.y, m_target.z - eye.z});

  // World up, not the camera's. Crossing it with forward gives a right vector with no roll in it,
  // which is what keeps the horizon level however far the camera is tilted. The pitch clamp is
  // what stops forward and up becoming parallel here, where the cross product would vanish.
  constexpr WorldPoint WORLD_UP = {0.0F, 1.0F, 0.0F};
  const WorldPoint right = Normalized(Cross(forward, WORLD_UP));
  const WorldPoint up = Cross(right, forward);

  return Basis{right, up, forward};
}

OrbitCamera::ScreenPoint OrbitCamera::Project(const WorldPoint& _world) const noexcept
{
  const WorldPoint eye = Position();
  const Basis basis = ViewBasis();

  const WorldPoint fromEye = {_world.x - eye.x, _world.y - eye.y, _world.z - eye.z};
  const float depth = Dot(fromEye, basis.forward);
  if (depth < MINIMUM_DEPTH)
  {
    return ScreenPoint{0.0F, 0.0F, depth, false};
  }

  const float across = Dot(fromEye, basis.right);
  const float upward = Dot(fromEye, basis.up);

  // The perspective divide. tan(fov/2) is the half-height of the view at unit depth; the aspect
  // widens it horizontally.
  const float halfHeightAtUnitDepth = std::tan(m_fieldOfViewRadians * 0.5F);
  const float aspect = m_viewportWidthPixels / m_viewportHeightPixels;

  const float normalizedX = (across / depth) / (halfHeightAtUnitDepth * aspect);
  const float normalizedY = (upward / depth) / halfHeightAtUnitDepth;

  return ScreenPoint{
    m_viewportXPixels + (normalizedX * 0.5F + 0.5F) * m_viewportWidthPixels,
    // Screen y runs down and the view's up runs up, so this one subtracts.
    m_viewportYPixels + (0.5F - normalizedY * 0.5F) * m_viewportHeightPixels,
    depth,
    true,
  };
}

OrbitCamera::ScreenPoint OrbitCamera::ProjectDirection(const WorldPoint& _direction) const noexcept
{
  const Basis basis = ViewBasis();

  // The same arithmetic as `Project`, with the eye subtraction gone. That single missing line is
  // what makes this exact at infinity: there is no position to be parallax against.
  const float depth = Dot(_direction, basis.forward);
  if (depth < MINIMUM_DEPTH)
  {
    return ScreenPoint{0.0F, 0.0F, depth, false};
  }

  const float across = Dot(_direction, basis.right);
  const float upward = Dot(_direction, basis.up);

  const float halfHeightAtUnitDepth = std::tan(m_fieldOfViewRadians * 0.5F);
  const float aspect = m_viewportWidthPixels / m_viewportHeightPixels;

  const float normalizedX = (across / depth) / (halfHeightAtUnitDepth * aspect);
  const float normalizedY = (upward / depth) / halfHeightAtUnitDepth;

  return ScreenPoint{
    m_viewportXPixels + (normalizedX * 0.5F + 0.5F) * m_viewportWidthPixels,
    m_viewportYPixels + (0.5F - normalizedY * 0.5F) * m_viewportHeightPixels,
    depth,
    true,
  };
}

bool OrbitCamera::InsideViewport(const ScreenPoint& _point) const noexcept
{
  return _point.visible && _point.xPixels >= m_viewportXPixels && _point.xPixels < m_viewportXPixels + m_viewportWidthPixels &&
         _point.yPixels >= m_viewportYPixels && _point.yPixels < m_viewportYPixels + m_viewportHeightPixels;
}

float OrbitCamera::PixelsPerWorldUnitAt(float _depth) const noexcept
{
  if (_depth < MINIMUM_DEPTH)
  {
    return 0.0F;
  }

  // The view is 2 * tan(fov/2) * depth world units tall and m_viewportHeightPixels pixels tall.
  const float worldUnitsTall = 2.0F * std::tan(m_fieldOfViewRadians * 0.5F) * _depth;
  return m_viewportHeightPixels / worldUnitsTall;
}

} // namespace Neuron
