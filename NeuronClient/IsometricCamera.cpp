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

IsometricCamera::IsometricCamera(float _virtualWidthPixels, float _virtualHeightPixels) noexcept
  : m_virtualWidthPixels(_virtualWidthPixels),
    m_virtualHeightPixels(_virtualHeightPixels)
{
}

void IsometricCamera::Follow(const WorldPoint& _target) noexcept
{
  m_target = _target;

  // std::round, not a cast: a cast truncates toward zero, which puts a seam at the origin where
  // the camera would jump two pixels instead of one as it crosses.
  m_snappedTargetXPixels = std::round((_target.x - _target.z) * HALF_TILE_WIDTH_PIXELS);
  m_snappedTargetYPixels = std::round((_target.x + _target.z) * HALF_TILE_HEIGHT_PIXELS - _target.y * HEIGHT_PIXELS_PER_UNIT);
}

IsometricCamera::ScreenPoint IsometricCamera::Project(const WorldPoint& _world) const noexcept
{
  const float xPixels = (_world.x - _world.z) * HALF_TILE_WIDTH_PIXELS;
  const float yPixels = (_world.x + _world.z) * HALF_TILE_HEIGHT_PIXELS - _world.y * HEIGHT_PIXELS_PER_UNIT;

  return ScreenPoint{
    (xPixels - m_snappedTargetXPixels) + m_virtualWidthPixels * 0.5F,
    (yPixels - m_snappedTargetYPixels) + m_virtualHeightPixels * 0.5F,
  };
}

IsometricCamera::WorldPoint IsometricCamera::UnprojectToGround(float _xPixels, float _yPixels) const noexcept
{
  // Undo the centering and the snap to get back to the pixel offsets the projection produced.
  const float xFromOrigin = (_xPixels - m_virtualWidthPixels * 0.5F) + m_snappedTargetXPixels;
  const float yFromOrigin = (_yPixels - m_virtualHeightPixels * 0.5F) + m_snappedTargetYPixels;

  // With y fixed at 0 the projection is two equations in x and z, and it inverts in one step:
  //   xFromOrigin = (x - z) * HALF_TILE_WIDTH_PIXELS
  //   yFromOrigin = (x + z) * HALF_TILE_HEIGHT_PIXELS
  const float difference = xFromOrigin / HALF_TILE_WIDTH_PIXELS; // x - z
  const float sum = yFromOrigin / HALF_TILE_HEIGHT_PIXELS;       // x + z

  return WorldPoint{(sum + difference) * 0.5F, 0.0F, (sum - difference) * 0.5F};
}

std::array<float, 16> IsometricCamera::ViewProjection() const noexcept
{
  // Clip space is [-1, 1] across the screen with y up, and [0, 1] in depth with 0 nearest.
  const float clipPerPixelX = 2.0F / m_virtualWidthPixels;
  const float clipPerPixelY = 2.0F / m_virtualHeightPixels;
  const float clipPerUnitDepth = INVERSE_SQRT_3 / (2.0F * DEPTH_HALF_RANGE_UNITS);

  const float a = HALF_TILE_WIDTH_PIXELS * clipPerPixelX;
  const float b = HALF_TILE_HEIGHT_PIXELS * clipPerPixelY;
  const float c = HEIGHT_PIXELS_PER_UNIT * clipPerPixelY;

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
