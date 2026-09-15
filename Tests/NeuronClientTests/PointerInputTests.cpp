// PointerInputTests.cpp -- the pointer: a WM_POINTER* message into a canvas position, at the identity
// presentation and at one that is not (ADR-075).

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

// The half of the click that is not the camera: a WM_POINTERDOWN carrying SCREEN coordinates
// becoming a point on the 1280x720 screen. The camera turns that into a world point and is tested
// above; between them they are the whole of what a tap does.
//
// This needs a real window, because the conversion is ScreenToClient and that is a property of a
// window rather than arithmetic. It is created off-screen and never shown.
TEST_CLASS(PointerInputTests)
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
    windowClass.lpszClassName = L"NeuronClientTestsPointerHost";
    RegisterClassExW(&windowClass);

    // WS_POPUP, so the client area starts exactly at the window's top-left and the expected
    // numbers below are arithmetic rather than a guess about how thick a caption is.
    m_window = CreateWindowExW(0, L"NeuronClientTestsPointerHost", L"", WS_POPUP, WINDOW_LEFT, WINDOW_TOP, 1280, 720, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    Assert::IsNotNull(m_window, L"the test needs a window to convert screen coordinates against");
  }

  TEST_METHOD_CLEANUP(DestroyHostWindow)
  {
    if (m_window != nullptr)
    {
      DestroyWindow(m_window);
      m_window = nullptr;
    }
  }

  /// Packs a screen point the way Windows packs it into WM_POINTERDOWN's lParam.
  [[nodiscard]] static LPARAM PackScreenPoint(int _screenX, int _screenY)
  {
    return static_cast<LPARAM>((static_cast<std::uint32_t>(_screenY & 0xFFFF) << 16) | static_cast<std::uint32_t>(_screenX & 0xFFFF));
  }

  TEST_METHOD(NothingIsPendingBeforeAPointerDown)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
  }

  TEST_METHOD(APointerDownBecomesAScreenPixel)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    // The window's client area starts at (300, 200) on screen, so this screen point is client
    // (400, 300) -- and, with the client area now exactly the framebuffer, screen pixel (400, 300)
    // too. Before ADR-011 this arrived as virtual texel (200, 150).
    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(400.0F, x, 0.0F);
    Assert::AreEqual(300.0F, y, 0.0F);
  }

  TEST_METHOD(TheTopLeftOfTheClientAreaIsTheOrigin)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);
    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT, WINDOW_TOP)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT, WINDOW_TOP)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(0.0F, x, 0.0F);
    Assert::AreEqual(0.0F, y, 0.0F);
  }

  TEST_METHOD(TakingAClickClearsIt)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);
    input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100));
    input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::IsFalse(input.TakeClick(x, y), L"a click is delivered once");
  }

  // Only the most recent tap survives: the ship goes where the player last pointed.
  TEST_METHOD(ASecondTapReplacesAnUnreadFirst)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);
    input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100));
    input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100));
    input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 900, WINDOW_TOP + 500));
    input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT + 900, WINDOW_TOP + 500));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(900.0F, x, 0.0F);
    Assert::AreEqual(500.0F, y, 0.0F);
  }

  // There is no keyboard control in this game and no mouse-button handler either: with
  // EnableMouseInPointer on, a mouse click arrives as WM_POINTERDOWN and a wheel notch as
  // WM_POINTERWHEEL. Anything outside the pointer family is ignored, and asserting that is what
  // stops a second input path appearing by accident.
  TEST_METHOD(MessagesOutsideThePointerFamilyAreIgnored)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    Assert::IsFalse(input.HandleMessage(WM_LBUTTONDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100)));
    Assert::IsFalse(input.HandleMessage(WM_KEYDOWN, VK_SPACE, 0));
    Assert::IsFalse(input.HandleMessage(WM_RBUTTONDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

private:
  HWND m_window = nullptr;
};

// The pointer path at a presentation that is NOT the identity, which is the half of ADR-075 that
// PresentationTests cannot reach: these go through a real window, a real WM_POINTER* lParam and
// real ScreenToClient, and they are what says a button is pressed where it is drawn once the
// canvas stops being the client area.
//
// The window is 1920x1080 at scale 1, so the canvas sits at (320, 180) with a 320x180 border on
// every side. That is the same shape a 4K monitor gives at scale 2 and a phone gives in portrait;
// it is used here because it is the one a test can create on any machine.
TEST_CLASS(PointerCanvasTests)
{
public:
  static constexpr int WINDOW_LEFT = 300;
  static constexpr int WINDOW_TOP = 200;
  static constexpr int SURFACE_WIDTH = 1920;
  static constexpr int SURFACE_HEIGHT = 1080;

  /// A 320x180 letterbox on each side. Written out rather than derived, so that a wrong offset in
  /// Presentation::For cannot agree with a wrong offset here.
  static constexpr float LETTERBOX_X = 320.0F;
  static constexpr float LETTERBOX_Y = 180.0F;

  static constexpr Neuron::Presentation LETTERBOXED = Neuron::Presentation::For(SURFACE_WIDTH, SURFACE_HEIGHT, 1);

  TEST_METHOD_INITIALIZE(CreateHostWindow)
  {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NeuronClientTestsCanvasHost";
    RegisterClassExW(&windowClass);

    // WS_POPUP, so the client area starts exactly at the window's top-left, as in
    // PointerInputTests.
    m_window = CreateWindowExW(0, L"NeuronClientTestsCanvasHost", L"", WS_POPUP, WINDOW_LEFT, WINDOW_TOP, SURFACE_WIDTH, SURFACE_HEIGHT,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Assert::IsNotNull(m_window, L"the test needs a window to convert screen coordinates against");
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

  /// A screen point for a given CANVAS pixel: the window origin, plus the letterbox, plus it.
  [[nodiscard]] static LPARAM ScreenPointForCanvas(int _canvasX, int _canvasY)
  {
    return PackScreenPoint(WINDOW_LEFT + static_cast<int>(LETTERBOX_X) + _canvasX, WINDOW_TOP + static_cast<int>(LETTERBOX_Y) + _canvasY);
  }

  TEST_METHOD(ATapReportsTheCanvasPixelItLandedOn)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 1, ScreenPointForCanvas(640, 360)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 1, ScreenPointForCanvas(640, 360)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(640.0F, x);
    Assert::AreEqual(360.0F, y);
  }

  // The whole point of the stage: the press is reported where the thing was DRAWN, not where the
  // surface says the finger was.
  TEST_METHOD(TheCanvasOriginIsNotTheSurfaceOrigin)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 1, ScreenPointForCanvas(0, 0)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 1, ScreenPointForCanvas(0, 0)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(0.0F, x);
    Assert::AreEqual(0.0F, y);
  }

  // A press out in the black border starts nothing AND is not consumed, so the window's default
  // handling still happens. Both halves matter: consuming it would swallow a click that belongs to
  // nobody, and recording it would put a contact at a negative canvas coordinate.
  TEST_METHOD(APressInTheLetterboxIsNeitherRecordedNorConsumed)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    Assert::IsFalse(input.HandleMessage(WM_POINTERDOWN, 1, PackScreenPoint(WINDOW_LEFT + 10, WINDOW_TOP + 10)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 1, PackScreenPoint(WINDOW_LEFT + 10, WINDOW_TOP + 10)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
  }

  // And the other half of that rule. A drag that STARTED on the canvas keeps going when it leaves,
  // because a gesture that stuck at the edge of the canvas would be a defect a player can feel.
  TEST_METHOD(ADragThatLeavesTheCanvasKeepsDragging)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 1, ScreenPointForCanvas(40, 360)));
    // Out past the left edge of the canvas, into the letterbox: canvas x is negative here.
    Assert::IsTrue(
      input.HandleMessage(WM_POINTERUPDATE, 1, PackScreenPoint(WINDOW_LEFT + 10, WINDOW_TOP + static_cast<int>(LETTERBOX_Y) + 360)));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsTrue(input.TakeDrag(drag));

    // From canvas x=40 to canvas x=(10 - 320) = -310, so the movement is -350 canvas pixels.
    Assert::AreEqual(-350.0F, drag.deltaXPixels);
    Assert::AreEqual(40.0F, drag.originXPixels);
    Assert::AreEqual(360.0F, drag.originYPixels);
  }

  // Hover is a state, and out in the letterbox the state is "nothing is under the pointer".
  TEST_METHOD(HoverInTheLetterboxIsNoHoverAtAll)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    // WM_MOUSEMOVE's lParam is CLIENT pixels already, so these are surface coordinates.
    Assert::IsFalse(input.HandleMessage(WM_MOUSEMOVE, 0, PackScreenPoint(320 + 100, 180 + 50)));
    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.PointerPosition(x, y));
    Assert::AreEqual(100.0F, x);
    Assert::AreEqual(50.0F, y);

    Assert::IsFalse(input.HandleMessage(WM_MOUSEMOVE, 0, PackScreenPoint(10, 10)));
    Assert::IsFalse(input.PointerPosition(x, y));
  }

private:
  HWND m_window = nullptr;
};

} // namespace NeuronClientTests
