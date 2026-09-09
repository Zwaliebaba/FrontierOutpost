#pragma once

namespace Neuron
{

/// The 2:1 dimetric camera the game is seen through, and the inverse that turns a click back into
/// a point on the ground (ADR-003).
///
/// The projection is written as whole numbers of virtual pixels rather than derived from a yaw
/// and a pitch, because the numbers of pixels are what has to come out exact:
///
///     pixelX = (x - z) * HALF_TILE_WIDTH_PIXELS
///     pixelY = (x + z) * HALF_TILE_HEIGHT_PIXELS - y * HEIGHT_PIXELS_PER_UNIT
///
/// A one-unit square on the ground is therefore a diamond exactly 16 pixels wide and 8 tall. The
/// null direction of that map -- the direction a viewing ray runs -- works out to (1, 1, 1), so
/// this is true isometric with the vertical axis squashed by sqrt(3)/2, which is what pixel-art
/// "isometric" has always actually been. ADR-003 has the derivation and why 2:1 rather than the
/// unsquashed 1.732:1.
///
/// The camera follows a target and snaps to whole virtual pixels as it goes; a sub-pixel camera
/// makes every static thing on screen shimmer as it moves.
class IsometricCamera
{
public:
  /// Half the width and half the height, in virtual pixels, of the diamond a one-unit ground
  /// square projects to. The 2:1 ratio between them is the decision; the 8 is the zoom.
  static constexpr float HALF_TILE_WIDTH_PIXELS = 8.0F;
  static constexpr float HALF_TILE_HEIGHT_PIXELS = 4.0F;

  /// One unit of world height, in virtual pixels. Equal to the half-tile width, which is what
  /// makes the squash exactly sqrt(3)/2 of true isometric (ADR-003).
  static constexpr float HEIGHT_PIXELS_PER_UNIT = 8.0F;

  /// Half the depth the orthographic projection spans, in world units, centered on the target.
  /// Space is unbounded but the depth buffer is not; 4096 units either way is four thousand times
  /// the size of the ship and still leaves D32_FLOAT far more precision than a 640x400 screen can
  /// show.
  static constexpr float DEPTH_HALF_RANGE_UNITS = 4096.0F;

  struct WorldPoint
  {
    float x;
    float y;
    float z;
  };

  /// A point on the 640x400 virtual screen, in pixels from the top-left corner.
  struct ScreenPoint
  {
    float xPixels;
    float yPixels;
  };

  IsometricCamera(float _virtualWidthPixels, float _virtualHeightPixels) noexcept;

  /// Centers the view on a world point, rounded to whole virtual pixels.
  void Follow(const WorldPoint& _target) noexcept;

  /// World to virtual screen pixels, including the snap. The inverse of UnprojectToGround for any
  /// point with y == 0.
  [[nodiscard]] ScreenPoint Project(const WorldPoint& _world) const noexcept;

  /// A virtual screen pixel back onto the y = 0 plane.
  ///
  /// It cannot miss. The projection is orthographic and space is unbounded, so there is no
  /// horizon and no near plane to fall off -- every pixel on the screen is some point on the
  /// ground, which is why this returns a point rather than an optional (MVP-01 section 2).
  [[nodiscard]] WorldPoint UnprojectToGround(float _xPixels, float _yPixels) const noexcept;

  /// The world-to-clip matrix, row-major, for `mul(float4(position, 1), matrix)` in HLSL.
  [[nodiscard]] std::array<float, 16> ViewProjection() const noexcept;

  [[nodiscard]] WorldPoint Target() const noexcept
  {
    return m_target;
  }

private:
  float m_virtualWidthPixels;
  float m_virtualHeightPixels;
  WorldPoint m_target = {0.0F, 0.0F, 0.0F};

  /// The target's projected position, rounded to whole pixels. Everything is drawn relative to
  /// this, so rounding it once here is what keeps the whole scene on the pixel grid.
  float m_snappedTargetXPixels = 0.0F;
  float m_snappedTargetYPixels = 0.0F;
};

} // namespace Neuron
