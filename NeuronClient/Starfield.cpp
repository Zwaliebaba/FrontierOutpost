// Starfield.cpp -- the sky, as directions on a sphere.

#include "pch.h"
#include "Starfield.h"

#include "ShapeRenderer.h"

#include "Prng.h"

#include <algorithm>
#include <cmath>

namespace Neuron
{

namespace
{

/// The smallest and largest dot a star draws, in screen pixels.
constexpr float SMALLEST_STAR_PIXELS = 0.7F;
constexpr float LARGEST_STAR_PIXELS = 1.2F;

/// How much of the passed colour's alpha the faintest star keeps.
///
/// **Half, and not less, because a sub-pixel dot has no room to be subtle.** The first attempt put
/// this at 0.45 of a lower base colour and skewed the brightness harder, which is closer to a real
/// sky and looked like a worse one: most of the field went to 0.7-pixel dots too dim to see, so a
/// sky with the same number of stars in it read as an emptier sky. The range has to fit between
/// "clearly there" and "brighter than that".
constexpr float FAINTEST_SHARE = 0.5F;

/// How sharply the band falls away from the galactic plane, as a sine of the angle off it. At this
/// width a star a fifth of a radian off the plane gets about a third of the band's weight.
constexpr float BAND_WIDTH = 0.22F;

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

Starfield::Starfield(std::uint64_t _seed, std::int32_t _count, float _bandStrength)
{
  if (_count <= 0)
  {
    return;
  }

  const float band = std::clamp(_bandStrength, 0.0F, 1.0F);

  Prng prng{_seed};
  m_stars.reserve(static_cast<std::size_t>(_count));

  for (std::int32_t index = 0; index < _count; ++index)
  {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    // The band, by rejection.
    //
    // **The loop always terminates and it is worth saying why rather than hoping.** The acceptance
    // below never falls under `1 - band`, which at the default is 0.4, so a star takes about one
    // and a half draws and the chance of needing ten is a millionth. A rejection scheme with no
    // floor is the one that hangs; this one cannot.
    for (;;)
    {
      // **Uniform on the sphere needs the y draw to be the HEIGHT rather than an angle.** Drawing
      // two angles and calling them latitude and longitude bunches stars at the poles, which on a
      // map somebody can tip all the way to overhead is not subtle: the sky would visibly thicken
      // as the camera rose. Sampling y flat and taking the radius from it gives every band of equal
      // height equal area, which is Archimedes' result and is the whole of that fix.
      const float height = 2.0F * UnitDraw(prng) - 1.0F;
      const float ring = std::sqrt(std::max(0.0F, 1.0F - height * height));
      const float angle = 6.28318530717958647692F * UnitDraw(prng);

      x = ring * std::cos(angle);
      y = height;
      z = ring * std::sin(angle);

      // How far off the galactic plane, as the sine of the angle: nought on the plane, one at the
      // pole. The weight is a bell in that, so the band has soft edges rather than a rim.
      const float offPlane = std::abs(x * Starfield::GALACTIC_POLE_X + y * Starfield::GALACTIC_POLE_Y + z * Starfield::GALACTIC_POLE_Z);
      const float spread = offPlane / BAND_WIDTH;
      const float keep = (1.0F - band) + band * std::exp(-spread * spread);

      if (UnitDraw(prng) < keep)
      {
        break;
      }
    }

    // Skewed towards faint, because that is what a sky looks like -- but only to the power of one
    // and a half. Squared was tried and is the more realistic curve; it is also, at thirty stars in
    // a small pane, indistinguishable from having drawn fewer stars.
    const float draw = UnitDraw(prng);
    const float brightness = draw * std::sqrt(draw);
    const float radius = SMALLEST_STAR_PIXELS + (LARGEST_STAR_PIXELS - SMALLEST_STAR_PIXELS) * brightness;

    m_stars.push_back(Star{x, y, z, brightness, radius});
  }
}

void Starfield::Draw(ShapeRenderer& _shapes, const OrbitCamera& _camera, const Color& _color) const
{
  for (const Star& star : m_stars)
  {
    const OrbitCamera::ScreenPoint at = _camera.ProjectDirection(OrbitCamera::WorldPoint{star.x, star.y, star.z});
    if (!_camera.InsideViewport(at))
    {
      continue;
    }

    // Alpha rather than a dimmer colour, because the sky sits on a gradient rather than on black:
    // darkening the star towards grey would make the faint ones muddy against the lighter top of
    // the pane, while less of them lets the backdrop through, which is what distance looks like.
    const float share = FAINTEST_SHARE + (1.0F - FAINTEST_SHARE) * star.brightness;
    const Color shade = {_color.red, _color.green, _color.blue, static_cast<std::uint8_t>(static_cast<float>(_color.alpha) * share)};

    _shapes.FillEllipse(at.xPixels, at.yPixels, star.radiusPixels, star.radiusPixels, shade);
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
