// Starfield.cpp -- the sky, as directions on a sphere.

#include "pch.h"
#include "Starfield.h"

#include "ShapeRenderer.h"

#include "Prng.h"

#include <cmath>

namespace Neuron
{

namespace
{

/// The smallest and largest dot a star draws, in screen pixels. The range carries the only depth
/// cue a point source gets: brighter-looking stars read as nearer even though none of them is.
constexpr float SMALLEST_STAR_PIXELS = 0.7F;
constexpr float LARGEST_STAR_PIXELS = 1.2F;
constexpr std::uint32_t STAR_SIZE_STEPS = 6;

/// A draw in [0, 1), from the top 24 bits.
///
/// The TOP bits, not the bottom: splitmix64's final xor-shift mixes the high end best, and 24 bits
/// is exactly what a `float` mantissa holds, so every value is representable and none of them is
/// reached twice as often as its neighbour.
[[nodiscard]] float UnitDraw(Prng& _prng) noexcept
{
  constexpr float SCALE = 1.0F / 16777216.0F;
  return static_cast<float>(_prng.Next() >> 40) * SCALE;
}

} // namespace

Starfield::Starfield(std::uint64_t _seed, std::int32_t _count)
{
  if (_count <= 0)
  {
    return;
  }

  Prng prng{_seed};
  m_stars.reserve(static_cast<std::size_t>(_count));

  for (std::int32_t index = 0; index < _count; ++index)
  {
    // **Uniform on the sphere, and that needs the y draw to be the HEIGHT rather than an angle.**
    // Drawing two angles and calling them latitude and longitude bunches stars at the poles, which
    // on a map somebody can tip all the way to overhead is not subtle: the sky visibly thickens as
    // the camera rises. Sampling y flat and taking the radius from it gives every band of equal
    // height equal area, which is Archimedes' result and is the whole of the fix.
    const float height = 2.0F * UnitDraw(prng) - 1.0F;
    const float ring = std::sqrt(std::max(0.0F, 1.0F - height * height));
    const float angle = 6.28318530717958647692F * UnitDraw(prng);

    const float steps = static_cast<float>(prng.Below(STAR_SIZE_STEPS));
    const float radius =
      SMALLEST_STAR_PIXELS + (LARGEST_STAR_PIXELS - SMALLEST_STAR_PIXELS) * steps / static_cast<float>(STAR_SIZE_STEPS - 1);

    m_stars.push_back(Star{ring * std::cos(angle), height, ring * std::sin(angle), radius});
  }
}

void Starfield::Draw(ShapeRenderer& _shapes, const OrbitCamera& _camera, const Color& _color) const
{
  for (const Star& star : m_stars)
  {
    const OrbitCamera::ScreenPoint at = _camera.ProjectDirection(OrbitCamera::WorldPoint{star.x, star.y, star.z});
    if (_camera.InsideViewport(at))
    {
      _shapes.FillEllipse(at.xPixels, at.yPixels, star.radiusPixels, star.radiusPixels, _color);
    }
  }
}

std::int32_t Starfield::VisibleCount(const OrbitCamera& _camera) const
{
  std::int32_t visible = 0;
  for (const Star& star : m_stars)
  {
    visible += _camera.InsideViewport(_camera.ProjectDirection(OrbitCamera::WorldPoint{star.x, star.y, star.z})) ? 1 : 0;
  }
  return visible;
}

} // namespace Neuron
