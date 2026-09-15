// OrbitCameraTests.cpp -- the camera the map is seen through (ADR-017).

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

// The camera the map is seen through (ADR-017). It is the piece of this screen whose bugs are
// hardest to see and easiest to talk yourself out of -- a mirrored axis or an inverted pitch
// still draws a plausible picture -- so the properties below are asserted rather than eyeballed.
TEST_CLASS(OrbitCameraTests)
{
public:
  static constexpr float PANE_X = 300.0F;
  static constexpr float PANE_Y = 48.0F;
  static constexpr float PANE_WIDTH = 650.0F;
  static constexpr float PANE_HEIGHT = 672.0F;

  static Neuron::OrbitCamera MakeCamera(float _yaw = 0.0F, float _pitch = 0.6F, float _distance = 1000.0F)
  {
    Neuron::OrbitCamera camera;
    camera.SetViewport(PANE_X, PANE_Y, PANE_WIDTH, PANE_HEIGHT);
    camera.SetTarget({0.0F, 0.0F, 0.0F});
    camera.SetDistance(_distance);
    camera.SetOrientation(_yaw, _pitch);
    return camera;
  }

  TEST_METHOD(TheTargetProjectsToTheCenterOfThePane)
  {
    for (const float yaw : {0.0F, 1.0F, -2.5F, 4.0F})
    {
      const Neuron::OrbitCamera camera = MakeCamera(yaw);
      const Neuron::OrbitCamera::ScreenPoint center = camera.Project({0.0F, 0.0F, 0.0F});

      Assert::IsTrue(center.visible);
      Assert::AreEqual(PANE_X + PANE_WIDTH * 0.5F, center.xPixels, 0.01F);
      Assert::AreEqual(PANE_Y + PANE_HEIGHT * 0.5F, center.yPixels, 0.01F);
    }
  }

  TEST_METHOD(TheEyeIsAtTheRequestedDistanceAndHeight)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.0F, 0.6F, 1000.0F);
    const Neuron::OrbitCamera::WorldPoint eye = camera.Position();

    Assert::AreEqual(1000.0F, std::sqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z), 0.01F);
    Assert::IsTrue(eye.y > 0.0F, L"the camera is above the plane it is looking at");
    // Yaw zero looks along -z, so the eye sits on +z.
    Assert::IsTrue(eye.z > 0.0F);
    Assert::AreEqual(0.0F, eye.x, 0.01F);
  }

  // The axis check that a wrong cross product would fail while still drawing a plausible map: at
  // yaw zero, world +x has to be screen RIGHT and world -z has to be further away.
  TEST_METHOD(AtYawZeroTheAxesAreNotMirrored)
  {
    const Neuron::OrbitCamera camera = MakeCamera();
    const Neuron::OrbitCamera::ScreenPoint center = camera.Project({0.0F, 0.0F, 0.0F});
    const Neuron::OrbitCamera::ScreenPoint right = camera.Project({100.0F, 0.0F, 0.0F});
    const Neuron::OrbitCamera::ScreenPoint away = camera.Project({0.0F, 0.0F, -100.0F});
    const Neuron::OrbitCamera::ScreenPoint up = camera.Project({0.0F, 100.0F, 0.0F});

    Assert::IsTrue(right.xPixels > center.xPixels, L"world +x is screen right");
    Assert::IsTrue(away.depth > center.depth, L"world -z is further from the eye");
    Assert::IsTrue(away.yPixels < center.yPixels, L"and further away is higher up the screen");
    Assert::IsTrue(up.yPixels < center.yPixels, L"world +y is screen up");
  }

  // Perspective, not orthographic: the same object is bigger when it is nearer. This is the whole
  // reason the projection was replaced.
  TEST_METHOD(NearerIsLarger)
  {
    const Neuron::OrbitCamera camera = MakeCamera();
    // `close`/`distant`, not `near`/`far`: <windows.h> still defines both of those to nothing.
    const float close = camera.PixelsPerWorldUnitAt(500.0F);
    const float distant = camera.PixelsPerWorldUnitAt(1500.0F);

    Assert::IsTrue(close > distant);
    Assert::AreEqual(3.0F, close / distant, 0.001F, L"three times nearer is three times bigger");
  }

  TEST_METHOD(PitchIsClampedAndYawIsNot)
  {
    Neuron::OrbitCamera camera = MakeCamera();

    camera.SetOrientation(0.0F, 100.0F);
    Assert::AreEqual(Neuron::OrbitCamera::MAX_PITCH_RADIANS, camera.PitchRadians(), 0.0001F);
    camera.SetOrientation(0.0F, -100.0F);
    Assert::AreEqual(Neuron::OrbitCamera::MIN_PITCH_RADIANS, camera.PitchRadians(), 0.0001F);

    camera.SetOrientation(50.0F, 0.6F);
    Assert::AreEqual(50.0F, camera.YawRadians(), 0.0001F, L"yaw runs as far as the player drags");
  }

  // Orbiting must not move the thing being looked at, at any angle. If it does, the map drifts
  // under the player as they turn it.
  TEST_METHOD(OrbitingKeepsTheTargetPutAndTheDistanceFixed)
  {
    Neuron::OrbitCamera camera = MakeCamera();

    for (std::int32_t step = 0; step < 24; ++step)
    {
      camera.Orbit(0.31F, 0.07F);

      const Neuron::OrbitCamera::WorldPoint eye = camera.Position();
      Assert::AreEqual(1000.0F, std::sqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z), 0.05F);

      const Neuron::OrbitCamera::ScreenPoint center = camera.Project({0.0F, 0.0F, 0.0F});
      Assert::AreEqual(PANE_X + PANE_WIDTH * 0.5F, center.xPixels, 0.05F);
      Assert::AreEqual(PANE_Y + PANE_HEIGHT * 0.5F, center.yPixels, 0.05F);
    }
  }

  // A pan slides the picture without turning it or changing its scale, which is what lets the map
  // centre a system without also zooming it (ADR-115). The signs are the part that draws a
  // plausible picture while being wrong, so both are asserted at every angle rather than eyeballed.
  TEST_METHOD(APanSlidesThePictureByTheseExactPixels)
  {
    for (const float yaw : {0.0F, 0.9F, -2.2F, 4.4F})
    {
      for (const float pitch : {0.2F, 0.62F, 1.3F})
      {
        Neuron::OrbitCamera camera = MakeCamera(yaw, pitch);
        const Neuron::OrbitCamera::WorldPoint aimed = {0.0F, 0.0F, 0.0F};
        const Neuron::OrbitCamera::ScreenPoint before = camera.Project(aimed);

        // Up the pane, which is what an open sheet asks for: a negative y.
        camera.PanPixels(0.0F, -175.0F);
        const Neuron::OrbitCamera::ScreenPoint lifted = camera.Project(aimed);

        Assert::AreEqual(175.0F, before.yPixels - lifted.yPixels, 0.01F, L"a lift did not move the picture up by its pixels");
        Assert::AreEqual(before.xPixels, lifted.xPixels, 0.01F, L"a vertical pan moved the picture sideways");
        Assert::AreEqual(before.depth, lifted.depth, 0.01F, L"a pan changed how far away the aimed point is");

        camera.PanPixels(60.0F, 0.0F);
        const Neuron::OrbitCamera::ScreenPoint across = camera.Project(aimed);
        Assert::AreEqual(60.0F, across.xPixels - lifted.xPixels, 0.01F, L"a positive x did not move the picture right");
      }
    }
  }

  // A pan is not an orbit and not a dolly: the eye keeps its angles and its distance from whatever
  // it is now looking at, so nothing rotates and nothing changes size. And it does move the eye,
  // which is the half a test of the angles alone would pass without.
  TEST_METHOD(APanTurnsNothingAndKeepsTheDistance)
  {
    Neuron::OrbitCamera camera = MakeCamera(0.9F, 0.62F);
    const Neuron::OrbitCamera::WorldPoint was = camera.Position();
    camera.PanPixels(-120.0F, 250.0F);
    const Neuron::OrbitCamera::WorldPoint now = camera.Position();

    Assert::AreEqual(0.9F, camera.YawRadians(), 0.0001F);
    Assert::AreEqual(0.62F, camera.PitchRadians(), 0.0001F);
    Assert::AreEqual(1000.0F, camera.Distance(), 0.0001F, L"a pan dollied the eye in or out");

    const float moved =
      std::sqrt((now.x - was.x) * (now.x - was.x) + (now.y - was.y) * (now.y - was.y) + (now.z - was.z) * (now.z - was.z));
    Assert::IsTrue(moved > 1.0F, L"a pan of 120 by 250 pixels left the eye where it was");
  }

  // A point level with or behind the eye has no projection. Drawing one anyway mirrors it through
  // the camera, which puts a lane straight across the pane.
  TEST_METHOD(PointsBehindTheEyeAreNotVisible)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.0F, 0.6F, 1000.0F);
    const Neuron::OrbitCamera::WorldPoint eye = camera.Position();

    const Neuron::OrbitCamera::ScreenPoint behind = camera.Project({eye.x, eye.y + 10.0F, eye.z + 500.0F});
    Assert::IsFalse(behind.visible);

    const Neuron::OrbitCamera::ScreenPoint front = camera.Project({0.0F, 0.0F, 0.0F});
    Assert::IsTrue(front.visible);
  }

  // Yawing by a full turn is the same camera. Worth pinning because the yaw is deliberately left
  // unwrapped, and an implementation that accumulated error would drift over a long session.
  TEST_METHOD(AFullTurnComesBackToWhereItStarted)
  {
    const Neuron::OrbitCamera before = MakeCamera(0.4F);
    const Neuron::OrbitCamera after = MakeCamera(0.4F + 2.0F * 3.14159265358979323846F);

    const Neuron::OrbitCamera::ScreenPoint a = before.Project({120.0F, 30.0F, -80.0F});
    const Neuron::OrbitCamera::ScreenPoint b = after.Project({120.0F, 30.0F, -80.0F});

    Assert::AreEqual(a.xPixels, b.xPixels, 0.05F);
    Assert::AreEqual(a.yPixels, b.yPixels, 0.05F);
  }

  /// A world point through the matrix, as the GPU would take it: row vector times row-major
  /// matrix, then the homogeneous divide, then the viewport transform onto the pane.
  struct ThroughTheMatrix
  {
    float xPixels;
    float yPixels;
    float depthZeroToOne;
    float w;
  };

  static ThroughTheMatrix PushThrough(const std::array<float, 16>& _matrix, const Neuron::OrbitCamera::WorldPoint& _point)
  {
    const std::array<float, 4> row = {_point.x, _point.y, _point.z, 1.0F};
    std::array<float, 4> clip = {};
    for (std::size_t column = 0; column < 4; ++column)
    {
      for (std::size_t inner = 0; inner < 4; ++inner)
      {
        clip[column] += row[inner] * _matrix[inner * 4 + column];
      }
    }
    const float ndcX = clip[0] / clip[3];
    const float ndcY = clip[1] / clip[3];
    return ThroughTheMatrix{PANE_X + (ndcX * 0.5F + 0.5F) * PANE_WIDTH, PANE_Y + (0.5F - ndcY * 0.5F) * PANE_HEIGHT, clip[2] / clip[3],
                            clip[3]};
  }

  // The matrix and `Project` are two statements of one camera, and this is what keeps them one
  // (ADR-103). The mesh pass puts a ball where the matrix says and the label where `Project` says;
  // the day they drift, every station's name floats away from its station. Three points off every
  // axis, at several orientations, to a hundredth of a pixel.
  TEST_METHOD(TheMatrixLandsOnTheSamePixelAsProject)
  {
    constexpr std::array<Neuron::OrbitCamera::WorldPoint, 3> POINTS = {
      Neuron::OrbitCamera::WorldPoint{120.0F, 30.0F, -80.0F},
      Neuron::OrbitCamera::WorldPoint{-300.0F, 0.0F, 200.0F},
      Neuron::OrbitCamera::WorldPoint{45.0F, 60.0F, 310.0F},
    };

    for (const float yaw : {0.0F, 1.3F, -2.5F})
    {
      for (const float pitch : {0.2F, 0.62F, 1.3F})
      {
        const Neuron::OrbitCamera camera = MakeCamera(yaw, pitch);
        const std::array<float, 16> matrix = camera.ViewProjection();

        for (const Neuron::OrbitCamera::WorldPoint& point : POINTS)
        {
          const Neuron::OrbitCamera::ScreenPoint expected = camera.Project(point);
          const ThroughTheMatrix actual = PushThrough(matrix, point);

          Assert::IsTrue(expected.visible);
          Assert::AreEqual(expected.xPixels, actual.xPixels, 0.01F, L"x drifted between the matrix and Project");
          Assert::AreEqual(expected.yPixels, actual.yPixels, 0.01F, L"y drifted between the matrix and Project");
          Assert::AreEqual(expected.depth, actual.w, 0.01F, L"the divide is the same divide");
          Assert::IsTrue(actual.depthZeroToOne > 0.0F && actual.depthZeroToOne < 1.0F, L"a framed point is inside the depth range");
        }
      }
    }
  }

  // The depth range is Direct3D's: nearer is smaller, the near plane is zero and the far plane is
  // one, so `DepthState(true)`'s LESS puts the nearer ball in front.
  TEST_METHOD(TheMatrixMapsDepthNearToZeroAndFarToOne)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.0F, 0.6F, 1000.0F);
    const std::array<float, 16> matrix = camera.ViewProjection();
    const Neuron::OrbitCamera::WorldPoint eye = camera.Position();

    // Straight ahead of the eye, by the distance the planes sit at.
    const auto ahead = [&](float _depth)
    {
      const float scale = _depth / 1000.0F;
      return Neuron::OrbitCamera::WorldPoint{eye.x * (1.0F - scale), eye.y * (1.0F - scale), eye.z * (1.0F - scale)};
    };

    // A thousandth: the near point is one unit in front of an eye a thousand units out, which is
    // about where float's precision on the subtraction sits.
    Assert::AreEqual(0.0F, PushThrough(matrix, ahead(Neuron::OrbitCamera::NEAR_PLANE_WORLD_UNITS)).depthZeroToOne, 0.001F);
    Assert::AreEqual(1.0F, PushThrough(matrix, ahead(1000.0F * Neuron::OrbitCamera::FAR_PLANE_IN_DISTANCES)).depthZeroToOne, 0.001F);

    const float nearer = PushThrough(matrix, ahead(900.0F)).depthZeroToOne;
    const float further = PushThrough(matrix, ahead(1100.0F)).depthZeroToOne;
    Assert::IsTrue(nearer < further, L"nearer is smaller");

    // Behind the eye has a negative w, which is the matrix's way of saying what `visible` says.
    Assert::IsTrue(PushThrough(matrix, ahead(-10.0F)).w < 0.0F);
    Assert::IsFalse(camera.Project(ahead(-10.0F)).visible);
  }
};

} // namespace NeuronClientTests
