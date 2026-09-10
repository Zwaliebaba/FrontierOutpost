#pragma once

#include <algorithm>
#include <cstdint>

namespace Frontier
{

/// The tilted ground plane the galaxy is drawn on.
///
/// This is the projection in Design/Screens/README.md, verbatim. It is NOT a camera: there is no
/// eye position, no field of view and no matrix, just a curve chosen because it looks like a
/// horizon. Design/Screens/README.md calls the map geometry illustrative and the projection the
/// spec, so this is the part that has to be exact.
///
///     d  = y / 560                       depth, 0 at the horizon and 1 at the near edge
///     s  = 0.5 + 0.65 * d                scale, 0.5 far and 1.15 near
///     sx = 400 + (x - 400) * s           x converges on the centre line with distance
///     sy = 60 + 440 * (0.3d + 0.7d^2)    y is compressed towards the horizon, quadratically
///
/// The quadratic in `sy` is what makes the near half of the plane spread out and the far half
/// bunch up. A linear `sy` would give a plane seen edge-on from infinity, which reads flat.
///
/// WHAT SCALES AND WHAT DOES NOT. `s` scales node radii, stem heights and shadow ellipses -- the
/// geometry. It does NOT scale text: the reference draws every map label at one size, and the
/// game has one font at one size (ADR-014). A far label is the same 8px as a near one.
class MapProjection
{
public:
  /// The design space the graph is authored in. Everything in MatchState's positions is in these
  /// units.
  static constexpr float DESIGN_WIDTH = 800.0F;
  static constexpr float DESIGN_HEIGHT = 560.0F;

  static constexpr float HORIZON_Y = 60.0F;
  static constexpr float PLANE_DEPTH = 440.0F;
  static constexpr float CENTER_X = 400.0F;
  static constexpr float SCALE_AT_HORIZON = 0.5F;
  static constexpr float SCALE_RANGE = 0.65F;

  struct Point
  {
    float x;
    float y;
  };

  /// Builds a projection letterboxed into a pane. `meet`, never crop: the whole plane is always
  /// visible and the pane grows bars rather than losing map (README, `preserveAspectRatio`).
  MapProjection(float _paneXPixels, float _paneYPixels, float _paneWidthPixels, float _paneHeightPixels) noexcept
  {
    m_scale = std::min(_paneWidthPixels / DESIGN_WIDTH, _paneHeightPixels / DESIGN_HEIGHT);
    // Floored, so the whole map sits on whole pixels. Half a pixel of offset would put every
    // 8px label on a half pixel, which is the one thing the text pass must never be handed.
    m_offsetX = std::floor(_paneXPixels + (_paneWidthPixels - DESIGN_WIDTH * m_scale) * 0.5F);
    m_offsetY = std::floor(_paneYPixels + (_paneHeightPixels - DESIGN_HEIGHT * m_scale) * 0.5F);
  }

  /// The depth-dependent scale at a design-space y. Node sizes and stem heights multiply by this.
  [[nodiscard]] static float ScaleAt(float _designY) noexcept
  {
    return SCALE_AT_HORIZON + SCALE_RANGE * (_designY / DESIGN_HEIGHT);
  }

  /// Design space to the map's own 800x560 projected space, before the letterbox.
  [[nodiscard]] static Point ProjectDesign(float _designX, float _designY) noexcept
  {
    const float d = _designY / DESIGN_HEIGHT;
    const float s = SCALE_AT_HORIZON + SCALE_RANGE * d;
    return Point{CENTER_X + (_designX - CENTER_X) * s, HORIZON_Y + PLANE_DEPTH * (0.3F * d + 0.7F * d * d)};
  }

  /// Design space all the way to screen pixels.
  [[nodiscard]] Point Project(float _designX, float _designY) const noexcept
  {
    const Point p = ProjectDesign(_designX, _designY);
    return Point{m_offsetX + p.x * m_scale, m_offsetY + p.y * m_scale};
  }

  /// A length in projected space, in screen pixels. Node radii and stem heights are computed in
  /// projected space and then brought here, so they letterbox with everything else.
  [[nodiscard]] float ToScreen(float _projectedLength) const noexcept
  {
    return _projectedLength * m_scale;
  }

  [[nodiscard]] float LetterboxScale() const noexcept
  {
    return m_scale;
  }

private:
  float m_scale = 1.0F;
  float m_offsetX = 0.0F;
  float m_offsetY = 0.0F;
};

} // namespace Frontier
