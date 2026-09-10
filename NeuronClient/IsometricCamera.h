#pragma once

namespace Neuron
{

/// The 2:1 dimetric camera the game is seen through, and the inverse that turns a click back into
/// a point on the ground (ADR-003).
///
/// The projection is written as whole numbers of screen pixels rather than derived from a yaw and
/// a pitch, because the numbers of pixels are what has to come out exact. Writing w for
/// HalfTileWidthPixels():
///
///     pixelX = (x - z) * w
///     pixelY = (x + z) * (w / 2) - y * w
///
/// At the default zoom w is 16, so a one-unit square on the ground is a diamond exactly 32 pixels
/// wide and 16 tall. Zoom moves w through a list of even values and changes nothing else: the 2:1
/// stays 2:1 at every level, which is the point of the list being what it is (ADR-013). The
/// null direction of that map -- the direction a viewing ray runs -- works out to (1, 1, 1), so
/// this is true isometric with the vertical axis squashed by sqrt(3)/2, which is what pixel-art
/// "isometric" has always actually been. ADR-003 has the derivation and why 2:1 rather than the
/// unsquashed 1.732:1.
///
/// The camera follows a target and snaps to whole pixels as it goes; a sub-pixel camera makes
/// every static thing on screen shimmer as it moves.
class IsometricCamera
{
public:
  /// The zoom levels this camera has, as the half-width in screen pixels of the diamond a
  /// one-unit ground square projects to (ADR-013, superseding ADR-008).
  ///
  /// A LIST rather than a range, and every entry EVEN, because the half-height is half of this
  /// and both have to be whole numbers -- that is what keeps a lattice of world points on a
  /// lattice of pixels and the un-projection exact (ADR-003). An odd level would put the tile
  /// edge on half-pixel steps and the crisp staircase would go.
  ///
  /// The list is ADR-008's {4, 6, 8, 12, 16} doubled. It is the same ladder in the same units it
  /// always physically had: before 2026-09-10 a "pixel" here was a virtual one and reached the
  /// display as a 2x2 block, so 8 virtual pixels a ground unit was 16 physical ones. With the
  /// blow-up gone (ADR-011) the two are the same thing, and doubling the numbers is what keeps
  /// the picture the size it was rather than half of it (ADR-013).
  static constexpr std::array<float, 5> ZOOM_LEVELS_PIXELS = {8.0F, 12.0F, 16.0F, 24.0F, 32.0F};

  /// 16 pixels a ground unit: the scale everything in the game is drawn against.
  static constexpr std::size_t DEFAULT_ZOOM_INDEX = 2;
  static constexpr float DEFAULT_HALF_TILE_WIDTH_PIXELS = ZOOM_LEVELS_PIXELS[DEFAULT_ZOOM_INDEX];

  /// Half the depth the orthographic projection spans, in world units, centered on the target.
  /// Space is unbounded but the depth buffer is not; 4096 units either way is four thousand times
  /// the size of the ship and still leaves D32_FLOAT far more precision than a 1280x720 screen can
  /// show.
  static constexpr float DEPTH_HALF_RANGE_UNITS = 4096.0F;

  struct WorldPoint
  {
    float x;
    float y;
    float z;
  };

  /// A point on the 1280x720 screen, in pixels from the top-left corner.
  struct ScreenPoint
  {
    float xPixels;
    float yPixels;
  };

  IsometricCamera(float _screenWidthPixels, float _screenHeightPixels) noexcept;

  /// Centers the view on a world point, rounded to whole pixels.
  void Follow(const WorldPoint& _target) noexcept;

  /// Moves the zoom by whole steps through ZOOM_LEVELS_PIXELS, clamped at both ends. Positive
  /// zooms in.
  ///
  /// There is no anchor argument and there should not be one. Zooming usually keeps the point
  /// under the cursor fixed, but this camera always centers on whatever it follows (MVP-01
  /// section 2: the ship stays centered and space scrolls under it), so the anchor is the center
  /// of the screen by construction and there is nothing to pass.
  void ZoomBy(std::int32_t _steps) noexcept;

  /// Half the width, in screen pixels, of the diamond a one-unit ground square currently
  /// projects to. The half-height is half of this and one unit of world height is equal to it,
  /// which is what makes the squash exactly sqrt(3)/2 of true isometric (ADR-003).
  [[nodiscard]] float HalfTileWidthPixels() const noexcept
  {
    return ZOOM_LEVELS_PIXELS[m_zoomIndex];
  }

  [[nodiscard]] std::size_t ZoomIndex() const noexcept
  {
    return m_zoomIndex;
  }

  /// Where the camera is, in whole screen pixels, already snapped.
  ///
  /// Anything drawn as a function of screen position rather than of world position needs this --
  /// the starfield is the reason it exists. It is a whole number, which is the property that
  /// makes such a thing possible at all: a backdrop scrolled by a fractional offset resamples
  /// itself every frame and shimmers (ADR-003, ADR-010).
  [[nodiscard]] float SnappedTargetXPixels() const noexcept
  {
    return m_snappedTargetXPixels;
  }
  [[nodiscard]] float SnappedTargetYPixels() const noexcept
  {
    return m_snappedTargetYPixels;
  }

  /// World to screen pixels, including the snap. The inverse of UnprojectToGround for any point
  /// with y == 0.
  [[nodiscard]] ScreenPoint Project(const WorldPoint& _world) const noexcept;

  /// A screen pixel back onto the y = 0 plane.
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
  /// Recomputes the snapped target. Both Follow and ZoomBy need it: the snap is in pixels, so it
  /// depends on the scale as well as on where the camera is, and a zoom that did not re-snap
  /// would leave the whole scene half a pixel out until the next frame moved the ship.
  void UpdateSnap() noexcept;

  float m_screenWidthPixels;
  float m_screenHeightPixels;
  WorldPoint m_target = {0.0F, 0.0F, 0.0F};
  std::size_t m_zoomIndex = DEFAULT_ZOOM_INDEX;

  /// The target's projected position, rounded to whole pixels. Everything is drawn relative to
  /// this, so rounding it once here is what keeps the whole scene on the pixel grid.
  float m_snappedTargetXPixels = 0.0F;
  float m_snappedTargetYPixels = 0.0F;
};

} // namespace Neuron
