// StarfieldTests.cpp -- the sky (ADR-033).

#include "pch.h"
#include "CppUnitTest.h"

#include <algorithm>
#include <array>
#include <cmath>

// NeuronClient.h, not NeuronCore.h: it is the umbrella the library's own translation units
// compile against, so a suite that includes anything else is testing a header in a configuration
// nothing else ever builds it in.
#include "NeuronClient.h"

#include "Color.h"
#include "FontRenderer.h"
#include "MeshRenderer.h"
#include "OrbitCamera.h"
#include "PointerInput.h"
#include "Presentation.h"
#include "SceneTarget.h"
#include "ShapeRenderer.h"
#include "Starfield.h"
#include "TextField.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

// ---- The sky ------------------------------------------------------------------------------------
//
// ADR-032. Every test here is one the star field this replaced would have failed, which is the point
// of writing them: the old one was thirty dots slid sideways by a constant, and nothing in the suite
// touched it, so a sky moving at a tenth of the right rate looked exactly like a sky that worked.

TEST_CLASS(StarfieldTests)
{
public:
  static constexpr float PANE_X = 300.0F;
  static constexpr float PANE_Y = 48.0F;
  static constexpr float PANE_WIDTH = 650.0F;
  static constexpr float PANE_HEIGHT = 672.0F;

  /// The map's own tilt limits, so the numbers below are the ones the game gets.
  static constexpr float MIN_PITCH = Neuron::OrbitCamera::MIN_PITCH_RADIANS;
  static constexpr float MAX_PITCH = Neuron::OrbitCamera::MAX_PITCH_RADIANS;

  static Neuron::OrbitCamera MakeCamera(float _yaw = 0.0F, float _pitch = 0.62F)
  {
    Neuron::OrbitCamera camera;
    camera.SetViewport(PANE_X, PANE_Y, PANE_WIDTH, PANE_HEIGHT);
    camera.SetTarget({0.0F, 0.0F, 0.0F});
    camera.SetDistance(700.0F);
    camera.SetOrientation(_yaw, _pitch);
    return camera;
  }

  /// How many orientations the sweeps below visit. Integers rather than a float step, because a
  /// float accumulated in a loop drifts and its last iteration is a coin toss -- and because a
  /// sweep that quietly stopped one step early would weaken exactly the tests that need it not to.
  static constexpr std::int32_t PITCH_SAMPLES = 33;
  static constexpr std::int32_t YAW_SAMPLES = 24;

  [[nodiscard]] static float PitchAt(std::int32_t _sample) noexcept
  {
    return MIN_PITCH + (MAX_PITCH - MIN_PITCH) * static_cast<float>(_sample) / static_cast<float>(PITCH_SAMPLES - 1);
  }

  [[nodiscard]] static float YawAt(std::int32_t _sample) noexcept
  {
    constexpr float TURN = 6.28318530717958647692F;
    return TURN * static_cast<float>(_sample) / static_cast<float>(YAW_SAMPLES);
  }

  [[nodiscard]] static bool SameSky(const Neuron::Starfield& _a, const Neuron::Starfield& _b)
  {
    return std::ranges::equal(_a.Stars(), _b.Stars(), [](const Neuron::Starfield::Star& _first, const Neuron::Starfield::Star& _second)
                              { return _first.x == _second.x && _first.y == _second.y && _first.z == _second.z; });
  }

  TEST_METHOD(EveryStarIsADirection)
  {
    const Neuron::Starfield sky;
    Assert::AreEqual(static_cast<std::size_t>(Neuron::Starfield::DEFAULT_COUNT), sky.Stars().size());

    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const float length = std::sqrt(star.x * star.x + star.y * star.y + star.z * star.z);
      Assert::AreEqual(1.0F, length, 0.0005F, L"a star is a direction, so it is a unit vector");
      Assert::IsTrue(star.brightness >= 0.0F && star.brightness <= 1.0F);
      Assert::IsTrue(star.radiusPixels >= 0.7F && star.radiusPixels <= 1.2F);
    }
  }

  // Size follows brightness rather than being drawn beside it. A bright small star and a dim large
  // one are both things the eye reads as a contradiction, and two independent draws would produce
  // both.
  TEST_METHOD(SizeFollowsBrightness)
  {
    const Neuron::Starfield sky;

    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const float expected = 0.7F + 0.5F * star.brightness;
      Assert::AreEqual(expected, star.radiusPixels, 0.0005F);
    }
  }

  // Most stars faint, a few bright. A flat draw would give a field of uniformly middling dots,
  // which reads as a texture rather than as a sky -- so this asserts the shape of the distribution
  // and not merely its range.
  TEST_METHOD(MostStarsAreFaint)
  {
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 20000};

    std::int32_t faint = 0;
    std::int32_t bright = 0;
    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      faint += star.brightness < 0.4F ? 1 : 0;
      bright += star.brightness > 0.6F ? 1 : 0;
    }

    // Brightness is drawn to the power of one and a half, so about 54% fall under 0.4 and about
    // 29% over 0.6. Wide bounds: what is being asserted is the skew, not the exact curve.
    Assert::IsTrue(faint > bright * 3 / 2, L"a sky is mostly faint stars");
    Assert::IsTrue(bright > 0, L"but not uniformly dim ones");
  }

  TEST_METHOD(TheSameSeedIsTheSameSky)
  {
    Assert::IsTrue(SameSky(Neuron::Starfield{}, Neuron::Starfield{}),
                   L"a screenshot is only comparable with another one if the background is the same");
    Assert::IsFalse(SameSky(Neuron::Starfield{}, Neuron::Starfield{Neuron::Starfield::DEFAULT_SEED + 1}));
  }

  // Sampling two angles and calling them latitude and longitude would bunch stars at the poles, and
  // on a map that tips all the way to overhead that is not subtle -- the sky would visibly thicken
  // as the camera rose. Equal heights must hold equal numbers.
  TEST_METHOD(TheSkyIsEvenRatherThanBunchedAtThePoles)
  {
    // No band, because this is a test about the SAMPLING rather than about the look: a deliberate
    // concentration towards the galactic plane would mask exactly the accidental one being looked
    // for here.
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 20000, 0.0F};

    // Four bands of equal HEIGHT, which on a sphere are four bands of equal area.
    std::array<std::int32_t, 4> bands = {};
    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const auto band = static_cast<std::size_t>(std::clamp((star.y + 1.0F) * 0.5F * 4.0F, 0.0F, 3.999F));
      ++bands[band];
    }

    for (const std::int32_t count : bands)
    {
      Assert::IsTrue(count > 4500 && count < 5500, L"equal areas of sky must hold roughly equal numbers of stars");
    }
  }

  // The band, which is the deliberate unevenness. Measured over equal solid angle, the plane
  // carries about 2.2 times the density of the poles -- enough to read as a band, not so much that
  // it becomes a stripe with empty sky either side.
  TEST_METHOD(TheGalacticPlaneIsDenserThanItsPoles)
  {
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 20000};

    // Twenty degrees each side of the plane, against the two twenty-degree caps at its poles. The
    // areas differ, so the counts are divided by them before being compared.
    constexpr float TWENTY_DEGREES = 0.34906585F;
    const float planeArea = std::sin(TWENTY_DEGREES);
    const float poleArea = 1.0F - std::cos(TWENTY_DEGREES);

    std::int32_t nearPlane = 0;
    std::int32_t nearPoles = 0;
    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const float offPlane = std::abs(star.x * Neuron::Starfield::GALACTIC_POLE_X + star.y * Neuron::Starfield::GALACTIC_POLE_Y +
                                      star.z * Neuron::Starfield::GALACTIC_POLE_Z);
      nearPlane += offPlane < std::sin(TWENTY_DEGREES) ? 1 : 0;
      nearPoles += offPlane > std::cos(TWENTY_DEGREES) ? 1 : 0;
    }

    Assert::IsTrue(nearPoles > 0, L"a band is not a band if the rest of the sky is empty");

    const float density = (static_cast<float>(nearPlane) / planeArea) / (static_cast<float>(nearPoles) / poleArea);
    Assert::IsTrue(density > 2.5F && density < 6.0F, L"the band has to be visible without being a stripe");
  }

  // Turning the band off gives back an even sphere. This is what says the concentration is a choice
  // rather than something the sampling does on its own.
  TEST_METHOD(NoBandIsAnEvenSky)
  {
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 20000, 0.0F};

    constexpr float TWENTY_DEGREES = 0.34906585F;
    std::int32_t nearPlane = 0;
    std::int32_t nearPoles = 0;
    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const float offPlane = std::abs(star.x * Neuron::Starfield::GALACTIC_POLE_X + star.y * Neuron::Starfield::GALACTIC_POLE_Y +
                                      star.z * Neuron::Starfield::GALACTIC_POLE_Z);
      nearPlane += offPlane < std::sin(TWENTY_DEGREES) ? 1 : 0;
      nearPoles += offPlane > std::cos(TWENTY_DEGREES) ? 1 : 0;
    }

    const float density =
      (static_cast<float>(nearPlane) / std::sin(TWENTY_DEGREES)) / (static_cast<float>(nearPoles) / (1.0F - std::cos(TWENTY_DEGREES)));
    Assert::IsTrue(density > 0.8F && density < 1.25F, L"with the band off, every part of the sky is the same density");
  }

  // The defect that started this. A sky at infinity sweeps at the focal-length rate, which is the
  // FASTEST anything moves rather than the slowest: what stands still under rotation is whatever is
  // painted on the glass. The field this replaced slid at 90 px/radian, about a tenth of this.
  TEST_METHOD(TheSkySweepsAtTheFocalRate)
  {
    const float focal = (PANE_HEIGHT * 0.5F) / std::tan(Neuron::OrbitCamera::DEFAULT_FIELD_OF_VIEW_RADIANS * 0.5F);
    Assert::IsTrue(focal > 900.0F && focal < 940.0F, L"the arithmetic this is measured against");

    // A star on the view axis, followed through a small yaw.
    const Neuron::OrbitCamera::WorldPoint axis = {0.0F, 0.0F, -1.0F};
    const Neuron::OrbitCamera::ScreenPoint at = MakeCamera(0.0F).ProjectDirection(axis);
    const Neuron::OrbitCamera::ScreenPoint after = MakeCamera(0.01F).ProjectDirection(axis);

    const float pixelsPerRadian = std::abs(after.xPixels - at.xPixels) / 0.01F;
    Assert::IsTrue(pixelsPerRadian > 900.0F, L"a sky that barely moves is a sky painted on the screen");
    Assert::IsTrue(pixelsPerRadian < 1400.0F, L"and one that races is not a sky either");
  }

  // A direction is periodic, so this is true by construction rather than by arithmetic -- which is
  // exactly why it is worth a test. The old field slid by an unbounded multiple of the yaw, so a
  // full turn left the galaxy where it started and the sky 565 pixels away from where it started.
  TEST_METHOD(AFullTurnPutsTheSkyBackWhereItWas)
  {
    const Neuron::Starfield sky;
    constexpr float TWO_PI = 6.28318530717958647692F;

    const Neuron::OrbitCamera before = MakeCamera(0.7F);
    const Neuron::OrbitCamera after = MakeCamera(0.7F + TWO_PI);

    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const Neuron::OrbitCamera::WorldPoint direction = {star.x, star.y, star.z};
      const Neuron::OrbitCamera::ScreenPoint a = before.ProjectDirection(direction);
      const Neuron::OrbitCamera::ScreenPoint b = after.ProjectDirection(direction);

      Assert::AreEqual(a.visible, b.visible);
      if (a.visible)
      {
        Assert::AreEqual(a.xPixels, b.xPixels, 0.05F, L"the same view of the galaxy must be the same view of the sky");
        Assert::AreEqual(a.yPixels, b.yPixels, 0.05F);
      }
    }
  }

  // The old field had a 510-pixel band of stars in a 672-pixel pane and no vertical wrap, so tilting
  // opened an empty strip along the top that reached 146 pixels at full pitch. A sphere has no edge
  // to run off.
  //
  // **What this asserts is that no part of the pane is SYSTEMATICALLY starved**, not that every
  // single view has a star in its top eighth. Those are different claims and the first one is the
  // one that matters. At about thirty stars in view, an eighth of the pane holds three or four, and
  // a handful of the four hundred orientations swept below happen to hold none -- which is what a
  // scattering of stars looks like rather than a bug. The old field failed this catastrophically:
  // its top eighth held zero at EVERY yaw once the camera was tilted past halfway.
  TEST_METHOD(NoTiltStarvesAnyPartOfTheSky)
  {
    const Neuron::Starfield sky;

    for (std::int32_t pitchSample = 0; pitchSample < PITCH_SAMPLES; ++pitchSample)
    {
      std::int32_t top = 0;
      std::int32_t bottom = 0;

      for (std::int32_t yawSample = 0; yawSample < YAW_SAMPLES; ++yawSample)
      {
        const Neuron::OrbitCamera camera = MakeCamera(YawAt(yawSample), PitchAt(pitchSample));
        for (const Neuron::Starfield::Star& star : sky.Stars())
        {
          const Neuron::OrbitCamera::ScreenPoint at = camera.ProjectDirection({star.x, star.y, star.z});
          if (!camera.InsideViewport(at))
          {
            continue;
          }
          top += at.yPixels < PANE_Y + PANE_HEIGHT / 8.0F ? 1 : 0;
          bottom += at.yPixels >= PANE_Y + PANE_HEIGHT * 7.0F / 8.0F ? 1 : 0;
        }
      }

      Assert::IsTrue(top > 0, L"the top of the pane was empty at every angle of some tilt");
      Assert::IsTrue(bottom > 0, L"and so was the bottom");

      // Both bands are equally far off the view axis, so a sphere puts about the same number in
      // each however the camera is tilted.
      //
      // The bounds are wide because the scatter genuinely is: swept across the whole tilt this
      // ratio was MEASURED at 0.44 to 1.29, centred on 1.0 with no trend. It is noisier than
      // independent sampling would suggest, and for a reason worth knowing -- yaw rotates about the
      // world's up axis, so turning the camera slides the same stars sideways without changing
      // their elevation. A sweep of yaw does not resample the vertical distribution; it is the same
      // draw seen twenty-four times. What these bounds catch is a TREND, which is what the field
      // this replaced had: its top eighth held zero at every yaw once the camera passed halfway.
      const float ratio = static_cast<float>(top) / static_cast<float>(bottom);
      Assert::IsTrue(ratio > 0.3F && ratio < 3.5F, L"one edge of the pane is systematically emptier than the other");
    }
  }

  // Thirty was what the authored field showed, and it looked right. Over a whole sphere that needs
  // several hundred, because only the frustum's share is ever on screen -- so this is the test that
  // keeps DEFAULT_COUNT honest if the field of view ever changes.
  TEST_METHOD(AboutThirtyStarsAreVisibleFromAnywhere)
  {
    const Neuron::Starfield sky;

    std::int32_t fewest = 1000000;
    std::int32_t most = 0;
    for (std::int32_t pitchSample = 0; pitchSample < PITCH_SAMPLES; ++pitchSample)
    {
      for (std::int32_t yawSample = 0; yawSample < YAW_SAMPLES; ++yawSample)
      {
        const std::int32_t visible = sky.VisibleCount(MakeCamera(YawAt(yawSample), PitchAt(pitchSample)));
        fewest = std::min(fewest, visible);
        most = std::max(most, visible);
      }
    }

    // Measured across the whole tilt and a full turn: 13 at the emptiest angle, 89 at the fullest,
    // averaging about thirty -- which is what the authored field showed and what DEFAULT_COUNT was
    // chosen to reproduce. The spread is wide because of the band: looking along it shows far more
    // than looking across it, which is the whole point of having one.
    Assert::IsTrue(fewest >= 10, L"the sky must never be nearly empty from any angle");
    Assert::IsTrue(most <= 110, L"nor crowded past being a backdrop");
  }

  // A direction behind the eye has no projection, and the half of the sky behind the camera is
  // always exactly that. Drawing it would put stars in the pane twice over, mirrored.
  TEST_METHOD(HalfTheSkyIsAlwaysBehindYou)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.0F, 0.62F);

    Assert::IsTrue(camera.ProjectDirection({0.0F, 0.0F, -1.0F}).visible, L"straight ahead");
    Assert::IsFalse(camera.ProjectDirection({0.0F, 0.0F, 1.0F}).visible, L"straight behind");

    const Neuron::Starfield sky;
    Assert::IsTrue(sky.VisibleCount(camera) < Neuron::Starfield::DEFAULT_COUNT / 2, L"at most half the sphere can ever be in front");
  }

  // `ProjectDirection` is `Project` with the eye subtraction removed, so a point far enough away
  // must agree with it. This is the cross-check that says the new method is the same lens rather
  // than a second, subtly different one.
  TEST_METHOD(ADirectionAgreesWithAPointAVeryLongWayOff)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.9F, 0.5F);
    const Neuron::OrbitCamera::WorldPoint eye = camera.Position();
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 64};

    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const Neuron::OrbitCamera::ScreenPoint direction = camera.ProjectDirection({star.x, star.y, star.z});
      if (!camera.InsideViewport(direction))
      {
        continue;
      }

      constexpr float FAR_AWAY = 4.0e6F;
      const Neuron::OrbitCamera::ScreenPoint point =
        camera.Project({eye.x + star.x * FAR_AWAY, eye.y + star.y * FAR_AWAY, eye.z + star.z * FAR_AWAY});

      Assert::AreEqual(direction.xPixels, point.xPixels, 1.0F);
      Assert::AreEqual(direction.yPixels, point.yPixels, 1.0F);
    }
  }
};

} // namespace NeuronClientTests
