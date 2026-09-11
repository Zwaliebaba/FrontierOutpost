#pragma once

#include "Color.h"
#include "OrbitCamera.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

class ShapeRenderer;

/// The sky: stars as DIRECTIONS on a sphere around the camera, not as dots on the glass.
///
/// **This is the second starfield in this tree and the first one that is a sky.** The one it
/// replaces was thirty dots in screen space that slid sideways by a constant when the camera
/// turned, and the reasoning behind it (ADR-016) confused two different things:
///
/// > Stars are meant to be very distant, so they parallax barely at all
///
/// True, and about the wrong transform. Parallax comes from moving the eye's *position*, and at
/// infinity it really is zero. Orbiting also *rotates* the view, and under rotation a distant thing
/// sweeps across the frame at the full focal-length rate -- the fastest anything moves, not the
/// slowest. A sky has zero parallax and maximum sweep. The old constant was about a tenth of the
/// right rate, which is why the sky read as painted on the screen (ADR-032).
///
/// Everything that was wrong with the old one falls out of that one change of representation rather
/// than being fixed case by case: the sky comes back after a full turn because a direction is
/// periodic, it never opens an empty band because the sphere has no edge to run off, and stars
/// spread apart towards the corners of the frame because that is what `tan` does to a lens. None of
/// those are special cases here. They are consequences of the sky being a sphere.
///
/// **It is generated rather than authored or stored.** R13 leaves nowhere to put a star texture,
/// and a few hundred directions from a pinned hash are the same sky on every machine and every run
/// -- which matters more than it sounds, because a screenshot of the map is only comparable with
/// another one if the background is the same.
class Starfield
{
public:
  /// A star: a unit direction, and how big a dot it draws.
  struct Star
  {
    float x;
    float y;
    float z;
    /// In screen pixels, and it does NOT scale with zoom. A star is a point source: what changes
    /// with a telescope is how many you can see, not how wide one is.
    float radiusPixels;
  };

  /// How many stars the sky holds, over the WHOLE sphere.
  ///
  /// Only the frustum's share is ever on screen -- about one part in twenty-six at the map's field
  /// of view -- so this is roughly thirty visible at a time, which is what the authored field had.
  /// `AboutThirtyStarsAreVisibleFromAnywhere` is the test that keeps that true.
  static constexpr std::int32_t DEFAULT_COUNT = 800;

  /// Pinned by value, like every other seed in this tree. Changing it changes the sky.
  static constexpr std::uint64_t DEFAULT_SEED = 0x5354'4152'4649'454CULL;

  explicit Starfield(std::uint64_t _seed = DEFAULT_SEED, std::int32_t _count = DEFAULT_COUNT);

  [[nodiscard]] std::span<const Star> Stars() const noexcept
  {
    return m_stars;
  }

  /// Draws every star in front of the eye and inside the camera's viewport.
  ///
  /// Call it before anything else in the pane: it writes a backdrop and does no depth test, so
  /// whatever is drawn after it covers it.
  void Draw(ShapeRenderer& _shapes, const OrbitCamera& _camera, const Color& _color) const;

  /// How many stars land inside the viewport right now. Drawing uses it; the tests assert on it,
  /// which is the only reason it is public -- an empty sky is the failure this replaces and it has
  /// to be observable without reading pixels.
  [[nodiscard]] std::int32_t VisibleCount(const OrbitCamera& _camera) const;

private:
  std::vector<Star> m_stars;
};

} // namespace Neuron
