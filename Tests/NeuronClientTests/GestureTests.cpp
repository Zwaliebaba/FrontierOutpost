// GestureTests.cpp -- tap against drag, and the two producers of a zoom (ADR-008, ADR-016).

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

// Tap and drag are the same gesture until they are not, and telling them apart is the whole of
// what this suite is about. Getting it wrong is not subtle: a tap that fires on press opens a
// panel every time the player tries to rotate the map, and a drag that swallows short movements
// makes the screen feel stuck (ADR-016).
TEST_CLASS(DragInputTests)
{
public:
  /// These tests are written at scale 1 in a window that IS the canvas, so a client pixel is a
  /// canvas pixel and every expected number below is arithmetic on the window origin. The mapping
  /// at other scales is PresentationTests and PointerCanvasTests (ADR-075).
  static constexpr Neuron::Presentation AT_SCALE_ONE = Neuron::Presentation::For(1280, 720, 1);

  static constexpr int WINDOW_LEFT = 300;
  static constexpr int WINDOW_TOP = 200;

  TEST_METHOD_INITIALIZE(CreateHostWindow)
  {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NeuronClientTestsDragHost";
    RegisterClassExW(&windowClass);

    m_window = CreateWindowExW(0, L"NeuronClientTestsDragHost", L"", WS_POPUP, WINDOW_LEFT, WINDOW_TOP, 1280, 720, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    Assert::IsNotNull(m_window);
  }

  TEST_METHOD_CLEANUP(DestroyHostWindow)
  {
    if (m_window != nullptr)
    {
      DestroyWindow(m_window);
      m_window = nullptr;
    }
  }

  [[nodiscard]] static LPARAM PackScreenPoint(int _screenX, int _screenY)
  {
    return static_cast<LPARAM>((static_cast<std::uint32_t>(_screenY & 0xFFFF) << 16) | static_cast<std::uint32_t>(_screenX & 0xFFFF));
  }

  [[nodiscard]] static WPARAM PackPointer(std::uint32_t _pointerId)
  {
    return static_cast<WPARAM>(_pointerId);
  }

  // A press and a lift in the same place is a tap, and it arrives on the LIFT. Before the map
  // could be rotated this fired on the press; it cannot any more, because at press time there is
  // no way to know the finger is not about to drag.
  TEST_METHOD(APressAndLiftIsATapAndArrivesOnTheLift)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y), L"a press alone is not yet a tap");

    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(500.0F, x, 0.0F);
    Assert::AreEqual(300.0F, y, 0.0F);
  }

  // Movement inside the slop is a finger that did not hold still, not a gesture.
  TEST_METHOD(AWobbleWithinTheSlopIsStillATap)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 502, WINDOW_TOP + 301));
    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 502, WINDOW_TOP + 301));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsFalse(input.TakeDrag(drag), L"a wobble is not a drag");

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(500.0F, x, 0.0F, L"and the tap is where the finger went down, not where it wandered to");
  }

  TEST_METHOD(MovingPastTheSlopIsADragAndCancelsTheTap)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 560, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 560, WINDOW_TOP + 300));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y), L"a drag must not also tap whatever it started on");
  }

  // The movement made BEFORE the slop was crossed counts, or a quick flick loses its first few
  // pixels and the map lags the finger.
  TEST_METHOD(ADragReportsEveryPixelIncludingTheSlop)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 560, WINDOW_TOP + 320));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsTrue(input.TakeDrag(drag));
    Assert::AreEqual(60.0F, drag.deltaXPixels, 0.0F);
    Assert::AreEqual(20.0F, drag.deltaYPixels, 0.0F);
    Assert::AreEqual(500.0F, drag.originXPixels, 0.0F, L"the drag belongs to where it began");
    Assert::AreEqual(300.0F, drag.originYPixels, 0.0F);
  }

  // Movement accumulates between frames and is consumed once. A frame that took longer than usual
  // must rotate by everything the finger did during it.
  TEST_METHOD(MovementAccumulatesAndIsConsumedOnce)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 540, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 570, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 600, WINDOW_TOP + 300));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsTrue(input.TakeDrag(drag));
    Assert::AreEqual(100.0F, drag.deltaXPixels, 0.0F, L"40 + 30 + 30");

    Assert::IsFalse(input.TakeDrag(drag), L"and it is gone once taken");
  }

  // A second finger is a pinch. It must cancel both of the one-finger gestures, or a zoom would
  // also spin the map and tap whatever the first finger landed on.
  TEST_METHOD(ASecondContactCancelsTheDragAndTheTap)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERDOWN, PackPointer(2), PackScreenPoint(WINDOW_LEFT + 700, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsFalse(input.TakeDrag(drag), L"a pinch is not a drag");

    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300));
    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y), L"nor a tap");
  }

  // Losing capture abandons the gesture. A press the window never saw the end of is not a tap.
  TEST_METHOD(LosingCaptureAbandonsThePress)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERCAPTURECHANGED, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
  }

private:
  HWND m_window = nullptr;
};

// Zoom has two producers -- the wheel and a pinch -- and one intent (ADR-008). These test the
// producers; nothing consumes the intent yet -- the camera that did was removed with the MVP-01
// scene (ADR-015), and the map on the main page has no zoom wired to it.
TEST_CLASS(ZoomInputTests)
{
public:
  /// These tests are written at scale 1 in a window that IS the canvas, so a client pixel is a
  /// canvas pixel and every expected number below is arithmetic on the window origin. The mapping
  /// at other scales is PresentationTests and PointerCanvasTests (ADR-075).
  static constexpr Neuron::Presentation AT_SCALE_ONE = Neuron::Presentation::For(1280, 720, 1);

  static constexpr int WINDOW_LEFT = 300;
  static constexpr int WINDOW_TOP = 200;
  static constexpr int CENTER_X = static_cast<int>(Neuron::SceneTarget::WIDTH_PIXELS) / 2;
  static constexpr int CENTER_Y = static_cast<int>(Neuron::SceneTarget::HEIGHT_PIXELS) / 2;

  TEST_METHOD_INITIALIZE(CreateHostWindow)
  {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NeuronClientTestsZoomHost";
    RegisterClassExW(&windowClass);

    m_window = CreateWindowExW(0, L"NeuronClientTestsZoomHost", L"", WS_POPUP, WINDOW_LEFT, WINDOW_TOP, 1280, 720, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    Assert::IsNotNull(m_window);
  }

  TEST_METHOD_CLEANUP(DestroyHostWindow)
  {
    if (m_window != nullptr)
    {
      DestroyWindow(m_window);
      m_window = nullptr;
    }
  }

  [[nodiscard]] static LPARAM PackScreenPoint(int _screenX, int _screenY)
  {
    return static_cast<LPARAM>((static_cast<std::uint32_t>(_screenY & 0xFFFF) << 16) | static_cast<std::uint32_t>(_screenX & 0xFFFF));
  }

  /// WM_POINTERWHEEL packs the pointer id in the low word and the wheel delta in the high word.
  [[nodiscard]] static WPARAM PackWheel(std::uint32_t _pointerId, int _delta)
  {
    return static_cast<WPARAM>((static_cast<std::uint32_t>(_delta & 0xFFFF) << 16) | (_pointerId & 0xFFFF));
  }

  [[nodiscard]] static WPARAM PackPointer(std::uint32_t _pointerId)
  {
    return static_cast<WPARAM>(_pointerId);
  }

  TEST_METHOD(NoInputMeansNoZoom)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

  TEST_METHOD(AWheelNotchIsOneStep)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    Assert::IsTrue(input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA), 0));
    Assert::AreEqual(1, input.TakeZoomSteps());

    Assert::IsTrue(input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, -WHEEL_DELTA), 0));
    Assert::AreEqual(-1, input.TakeZoomSteps(), L"scrolling back zooms back out");
  }

  // The classic mouse-wheel message is NOT handled, and that is the decision rather than an
  // oversight: with EnableMouseInPointer on, a real wheel was measured arriving as
  // WM_POINTERWHEEL (ADR-009), so a WM_MOUSEWHEEL case would be a second input path that never
  // runs -- exactly what MVP-01 section 2 rules out.
  TEST_METHOD(TheClassicMouseWheelMessageIsNotHandled)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    Assert::IsFalse(input.HandleMessage(WM_MOUSEWHEEL, PackWheel(0, WHEEL_DELTA), 0));
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

  TEST_METHOD(TakingTheZoomClearsIt)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA), 0);

    Assert::AreEqual(1, input.TakeZoomSteps());
    Assert::AreEqual(0, input.TakeZoomSteps(), L"a zoom is delivered once");
  }

  TEST_METHOD(NotchesInOneFrameAddUp)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA), 0);
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA), 0);
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA * 2), 0);

    Assert::AreEqual(4, input.TakeZoomSteps());
  }

  // A high-resolution wheel sends deltas smaller than a notch. Throwing the remainder away would
  // make such a wheel do nothing at all, however far it is turned -- which is the bug this keeps
  // out.
  TEST_METHOD(SubNotchWheelDeltasAccumulate)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    constexpr int THIRD_OF_A_NOTCH = WHEEL_DELTA / 3;
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, THIRD_OF_A_NOTCH), 0);
    Assert::AreEqual(0, input.TakeZoomSteps(), L"a third of a notch is not a step yet");

    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, THIRD_OF_A_NOTCH), 0);
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, THIRD_OF_A_NOTCH), 0);
    Assert::AreEqual(1, input.TakeZoomSteps(), L"three thirds are");
  }

  /// The two contacts of a pinch a given distance apart, horizontally, centered on the client
  /// area. The separation is in screen pixels, and so is everything else: there is no present
  /// scale to convert through any more (ADR-011).
  ///
  /// The right-hand contact is placed at left + separation rather than at center + half, so the
  /// distance between them is EXACTLY the requested one whether or not it is even. Halving it and
  /// mirroring would lose a pixel on an odd separation, which is enough to leave a test's
  /// separation a hair under the ratio it was supposed to cross -- and that says nothing about
  /// the code under test.
  static void PlaceContacts(Neuron::PointerInput& _input, UINT _message, float _separationPixels)
  {
    const auto separation = static_cast<int>(std::lround(_separationPixels));
    const int left = WINDOW_LEFT + CENTER_X - separation / 2;
    const int y = WINDOW_TOP + CENTER_Y;

    _input.HandleMessage(_message, PackPointer(1), PackScreenPoint(left, y));
    _input.HandleMessage(_message, PackPointer(2), PackScreenPoint(left + separation, y));
  }

  static void StartPinch(Neuron::PointerInput& _input, float _separationPixels)
  {
    PlaceContacts(_input, WM_POINTERDOWN, _separationPixels);
  }

  static void MovePinch(Neuron::PointerInput& _input, float _separationPixels)
  {
    PlaceContacts(_input, WM_POINTERUPDATE, _separationPixels);
  }

  TEST_METHOD(SpreadingTwoContactsZoomsIn)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    StartPinch(input, 100.0F);
    Assert::AreEqual(0, input.TakeZoomSteps(), L"putting two fingers down is not yet a zoom");

    MovePinch(input, 100.0F * Neuron::PointerInput::PINCH_STEP_RATIO);
    Assert::AreEqual(1, input.TakeZoomSteps());
  }

  TEST_METHOD(PinchingTwoContactsTogetherZoomsOut)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    StartPinch(input, 200.0F);
    MovePinch(input, 200.0F / Neuron::PointerInput::PINCH_STEP_RATIO);

    Assert::AreEqual(-1, input.TakeZoomSteps());
  }

  // One update can carry a large jump, from a fast pinch or a frame that was missed. Banking a
  // single step for it would make the zoom lag the fingers.
  TEST_METHOD(ALargeSpreadInOneUpdateBanksSeveralSteps)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    // 64 to 125 is exactly 1.25 cubed, and every value on the way -- 64, 80, 100, 125 -- lands on
    // a whole screen pixel and is exact in a float.
    StartPinch(input, 64.0F);
    MovePinch(input, 125.0F);

    Assert::AreEqual(3, input.TakeZoomSteps());
  }

  // The first finger of a pinch is indistinguishable from a tap until the second lands. Without
  // this, every pinch would also order the ship to wherever that first finger touched down.
  //
  // Since ADR-016 the tap is decided on the LIFT, so what the second contact cancels is the
  // pending PRESS -- the lift that follows must not produce a tap either.
  TEST_METHOD(ASecondContactCancelsThePendingTap)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 200, WINDOW_TOP + 300));
    float x = 0.0F;
    float y = 0.0F;

    input.HandleMessage(WM_POINTERDOWN, PackPointer(2), PackScreenPoint(WINDOW_LEFT + 600, WINDOW_TOP + 300));
    Assert::IsFalse(input.TakeClick(x, y), L"a pinch must not also fly the ship somewhere");
  }

  TEST_METHOD(OneContactStillTapsNormally)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(400.0F, x, 0.0F);
    Assert::AreEqual(300.0F, y, 0.0F);
    Assert::AreEqual(0, input.TakeZoomSteps(), L"and does not zoom");
  }

  // Lifting a finger ends the pinch. Putting it back down starts a new one from where the fingers
  // now are, rather than resuming against a baseline from before the gap.
  TEST_METHOD(LiftingAContactEndsThePinch)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    StartPinch(input, 100.0F);
    input.HandleMessage(WM_POINTERUP, PackPointer(2), PackScreenPoint(WINDOW_LEFT + 690, WINDOW_TOP + 400));

    // A lone contact moving a long way is a drag, not a pinch.
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 400));
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

  // A third finger is not a bigger pinch. Resting a hand on the glass must not make the zoom jump.
  TEST_METHOD(AThirdContactIsIgnored)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    StartPinch(input, 100.0F);
    input.HandleMessage(WM_POINTERDOWN, PackPointer(3), PackScreenPoint(WINDOW_LEFT + 1200, WINDOW_TOP + 700));
    Assert::AreEqual(0, input.TakeZoomSteps());

    MovePinch(input, 100.0F * Neuron::PointerInput::PINCH_STEP_RATIO);
    Assert::AreEqual(1, input.TakeZoomSteps(), L"the original two still drive the pinch");
  }

private:
  HWND m_window = nullptr;
};

} // namespace NeuronClientTests
