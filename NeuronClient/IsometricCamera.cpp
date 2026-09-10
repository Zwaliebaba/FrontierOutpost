// IsometricCamera.cpp -- the projection in ADR-003, and its inverse.

#include "pch.h"
#include "IsometricCamera.h"

namespace Neuron
{

namespace
{

// The viewing ray runs along (1, 1, 1), so depth along it is (x + y + z) / sqrt(3). Written out
// rather than computed, because a constant this file is built on should be readable as a number.
constexpr float INVERSE_SQRT_3 = 0.57735026918962576F;

} // namespace

IsometricCamera::IsometricCamera(float _screenWidthPixels, float _screenHeightPixels) noexcept
  : m_screenWidthPixels(_screenWidthPixels),
    m_screenHeightPixels(_screenHeightPixels)
{
}

void IsometricCamera::Follow(const WorldPoint& _target) noexcept
{
  m_target = _target;
  UpdateSnap();
}

void IsometricCamera::ZoomBy(std::int32_t _steps) noexcept
{
  const auto lastIndex = static_cast<std::int32_t>(ZOOM_LEVELS_PIXELS.size()) - 1;
  const std::int32_t wanted = static_cast<std::int32_t>(m_zoomIndex) + _steps;
  m_zoomIndex = static_cast<std::size_t>(std::clamp(wanted, 0, lastIndex));

  // The snap is in pixels and the pixels just changed size.
  UpdateSnap();
}

void IsometricCamera::UpdateSnap() noexcept
{
  const float halfWidth = HalfTileWidthPixels();

  // std::round, not a cast: a cast truncates toward zero, which puts a seam at the origin where
  // the camera would jump two pixels instead of one as it crosses.
  m_snappedTargetXPixels = std::round((m_target.x - m_target.z) * halfWidth);
  m_snappedTargetYPixels = std::round((m_target.x + m_target.z) * (halfWidth * 0.5F) - m_target.y * halfWidth);
}

IsometricCamera::ScreenPoint IsometricCamera::Project(const WorldPoint& _world) const noexcept
{
  const float halfWidth = HalfTileWidthPixels();
  const float xPixels = (_world.x - _world.z) * halfWidth;
  const float yPixels = (_world.x + _world.z) * (halfWidth * 0.5F) - _world.y * halfWidth;

  return ScreenPoint{
    (xPixels - m_snappedTargetXPixels) + m_screenWidthPixels * 0.5F,
    (yPixels - m_snappedTargetYPixels) + m_screenHeightPixels * 0.5F,
  };
}

IsometricCamera::WorldPoint IsometricCamera::UnprojectToGround(float _xPixels, float _yPixels) const noexcept
{
  // Undo the centering and the snap to get back to the pixel offsets the projection produced.
  const float xFromOrigin = (_xPixels - m_screenWidthPixels * 0.5F) + m_snappedTargetXPixels;
  const float yFromOrigin = (_yPixels - m_screenHeightPixels * 0.5F) + m_snappedTargetYPixels;

  // With y fixed at 0 the projection is two equations in x and z, and it inverts in one step:
  //   xFromOrigin = (x - z) * w
  //   yFromOrigin = (x + z) * (w / 2)
  const float halfWidth = HalfTileWidthPixels();
  const float difference = xFromOrigin / halfWidth;   // x - z
  const float sum = yFromOrigin / (halfWidth * 0.5F); // x + z

  return WorldPoint{(sum + difference) * 0.5F, 0.0F, (sum - difference) * 0.5F};
}

std::array<float, 16> IsometricCamera::ViewProjection() const noexcept
{
  // Clip space is [-1, 1] across the screen with y up, and [0, 1] in depth with 0 nearest.
  const float clipPerPixelX = 2.0F / m_screenWidthPixels;
  const float clipPerPixelY = 2.0F / m_screenHeightPixels;
  const float clipPerUnitDepth = INVERSE_SQRT_3 / (2.0F * DEPTH_HALF_RANGE_UNITS);

  const float halfWidth = HalfTileWidthPixels();
  const float a = halfWidth * clipPerPixelX;
  const float b = (halfWidth * 0.5F) * clipPerPixelY;
  const float c = halfWidth * clipPerPixelY;

  // Depth is measured from the target, so the full DEPTH_HALF_RANGE_UNITS either side is centered
  // on whatever the camera is following rather than on the world origin -- which matters because
  // space is unbounded and the ship never comes back (MVP-01 section 2).
  const float depthAtTarget = (m_target.x + m_target.y + m_target.z) * clipPerUnitDepth;

  // Row-major, for mul(float4(position, 1), matrix). Columns are clipX, clipY, clipZ, clipW.
  return std::array<float, 16>{
    // x contributes
    a,
    -b,
    -clipPerUnitDepth,
    0.0F,
    // y contributes: no horizontal shift at all, which is what makes a vertical world axis a
    // vertical column of pixels and keeps the hull's silhouette on the pixel grid.
    0.0F,
    c,
    -clipPerUnitDepth,
    0.0F,
    // z contributes
    -a,
    -b,
    -clipPerUnitDepth,
    0.0F,
    // translation
    -m_snappedTargetXPixels * clipPerPixelX,
    m_snappedTargetYPixels * clipPerPixelY,
    0.5F + depthAtTarget,
    1.0F,
  };
}

} // namespace Neuron
