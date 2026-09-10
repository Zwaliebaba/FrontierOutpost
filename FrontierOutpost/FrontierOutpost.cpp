// FrontierOutpost.cpp -- process entry point, and the composition root of the main page.
//
// WHAT THIS EXECUTABLE SHOWS, as of 2026-09-11, is the ops console in Design/Screens: digest,
// map, orders. It used to show the MVP-01 isometric ship scene, and that code -- MeshRenderer,
// IsometricCamera, Starfield, ShipMesh, StationMesh, ShipView, World, Session -- is still in the
// tree and still built and tested. It is not reachable from here, because the main page is a
// different screen of the same game rather than a mode of that one, and a mode switch is not
// something this task was asked for (ADR-014).
//
// This is the wizard's wWinMain reduced to what the game actually needs: one fixed-size,
// non-resizable window, no menu and no About dialog. The window is the presentation target
// described in Design/README.md -- a 1280x720 R8G8B8A8 framebuffer presented 1:1, so the client
// area is exactly the resolution the game renders and a rendered pixel is a physical one
// (ADR-011).
//
// There is no WM_PAINT handler and there must not be one: from the moment the swap chain exists
// it owns every pixel of the client area, and a BeginPaint/EndPaint pair racing it produces a
// flash and nothing else (AGENTS.md 4, NOGDI).

#include "pch.h"
#include "FrontierOutpost.h"

#include "Color.h"
#include "Device.h"
#include "FontRenderer.h"
#include "PointerInput.h"
#include "SceneTarget.h"
#include "ShapeRenderer.h"

#include "MainPage.h"
#include "MatchFixture.h"

#include <chrono>

namespace
{

// The screen the game presents. Not restated here: Neuron::SceneTarget owns the numbers, the
// window is created at exactly that size, and the swap chain is told the same thing -- which is
// what makes "the client area is the framebuffer" a fact rather than three constants that agree
// today (ADR-011).
constexpr int CLIENT_WIDTH = static_cast<int>(Neuron::SceneTarget::WIDTH_PIXELS);
constexpr int CLIENT_HEIGHT = static_cast<int>(Neuron::SceneTarget::HEIGHT_PIXELS);

constexpr wchar_t WINDOW_CLASS_NAME[] = L"FrontierOutpostWindow";
constexpr wchar_t WINDOW_TITLE[] = L"Frontier Outpost";

HINSTANCE g_instance = nullptr;
bool g_quitRequested = false;

/// The window procedure runs on the client thread and needs to reach the input state, which lives
/// in RunGame. A file-scope pointer is the plain Win32 answer and is what the rest of this file
/// already does with g_instance; it is set once, before the first message is dispatched.
Neuron::PointerInput* g_pointerInput = nullptr;

LRESULT CALLBACK WndProc(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam)
{
  if (g_pointerInput != nullptr && g_pointerInput->HandleMessage(_message, _wParam, _lParam))
  {
    return 0;
  }

  switch (_message)
  {
  case WM_DESTROY:
    g_quitRequested = true;
    PostQuitMessage(0);
    return 0;

  default:
    return DefWindowProcW(_window, _message, _wParam, _lParam);
  }
}

bool RegisterWindowClass(HINSTANCE _instance)
{
  WNDCLASSEXW windowClass = {};
  windowClass.cbSize = sizeof(WNDCLASSEXW);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = WndProc;
  windowClass.hInstance = _instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  // No background brush. NeuronCore.h defines NOGDI, so GetStockObject is not even declared here
  // -- which is the right answer rather than an obstacle: the swap chain owns every pixel of the
  // client area, and a GDI brush painting under it only produces a flash on resize.
  windowClass.hbrBackground = nullptr;
  windowClass.lpszClassName = WINDOW_CLASS_NAME;

  return RegisterClassExW(&windowClass) != 0;
}

// Sizes for the CLIENT area, not the window: AdjustWindowRect adds the border and caption, so the
// framebuffer is presented 1:1 rather than a few rows short of it.
//
// AdjustWindowRect assumes 96 DPI, and under per-monitor awareness the caption on a scaled
// display is not 96 DPI, so its answer is close rather than right. Rather than reach for
// AdjustWindowRectExForDpi and a DPI to pass it, the window is created and then measured: if the
// client area came out anything other than exact, the difference is added back. That is correct
// on every DPI, theme and Windows version without knowing anything about any of them -- and this
// has to be exact, because the client area IS the framebuffer: a row short is a row of the
// picture the player never sees.
HWND CreateMainWindow(HINSTANCE _instance, int _showCommand)
{
  constexpr DWORD STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

  RECT bounds = {0, 0, CLIENT_WIDTH, CLIENT_HEIGHT};
  AdjustWindowRect(&bounds, STYLE, FALSE);

  HWND window = CreateWindowExW(0, WINDOW_CLASS_NAME, WINDOW_TITLE, STYLE, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                                bounds.bottom - bounds.top, nullptr, nullptr, _instance, nullptr);
  if (window == nullptr)
  {
    return nullptr;
  }

  RECT clientArea = {};
  RECT outerArea = {};
  if (GetClientRect(window, &clientArea) != 0 && GetWindowRect(window, &outerArea) != 0)
  {
    const int widthShortfall = CLIENT_WIDTH - (clientArea.right - clientArea.left);
    const int heightShortfall = CLIENT_HEIGHT - (clientArea.bottom - clientArea.top);
    if (widthShortfall != 0 || heightShortfall != 0)
    {
      SetWindowPos(window, nullptr, 0, 0, (outerArea.right - outerArea.left) + widthShortfall,
                   (outerArea.bottom - outerArea.top) + heightShortfall, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }

  ShowWindow(window, _showCommand);
  UpdateWindow(window);
  return window;
}

/// PeekMessage, not GetMessage: the loop now has a frame to render whether or not the window has
/// anything to say. Returns false when the queue produced WM_QUIT.
bool PumpMessages()
{
  MSG message = {};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
  {
    if (message.message == WM_QUIT)
    {
      return false;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return !g_quitRequested;
}

int RunGame(HWND _window)
{
  Neuron::Device device;
  device.Create(_window, Neuron::SceneTarget::WIDTH_PIXELS, Neuron::SceneTarget::HEIGHT_PIXELS);

  // One shader-visible descriptor heap for the whole client: only one can be bound at a time, so
  // every renderer allocates its slots out of this (DescriptorHeap.h).
  Neuron::DescriptorHeap shaderVisibleHeap;
  shaderVisibleHeap.Create(device.Handle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, true);

  // Space is black, and on this screen it is also the colour of every rail behind every card.
  Neuron::SceneTarget screen;
  screen.Create(device.Handle(), Neuron::BLACK);

  // The two renderers the interface is made of, and the whole of what it needs: rectangles and
  // glyphs. There is no widget tree, no retained scene and no texture atlas beyond the font
  // (ADR-014).
  Neuron::ShapeRenderer shapes;
  shapes.Create(device.Handle());

  Neuron::FontRenderer text;
  text.Create(device, shaderVisibleHeap);

  // The match, from the fixture. When the server sends a digest this is the only line that
  // changes (MatchFixture.h).
  Frontier::MainPage page;
  page.Create(Frontier::MakeReferenceMatch());

  Neuron::PointerInput pointer;
  pointer.Create(_window);
  g_pointerInput = &pointer;

  auto previousFrame = std::chrono::steady_clock::now();

  while (PumpMessages())
  {
    const auto now = std::chrono::steady_clock::now();
    const double elapsedSeconds = std::chrono::duration<double>{now - previousFrame}.count();
    previousFrame = now;

    // The countdown is the only thing on this screen that moves on its own. Everything else
    // changes because the player did something or because a tick resolved.
    page.Update(elapsedSeconds);

    float tapXPixels = 0.0F;
    float tapYPixels = 0.0F;
    if (pointer.TakeClick(tapXPixels, tapYPixels))
    {
      page.HandleTap(tapXPixels, tapYPixels);
    }

    ID3D12GraphicsCommandList* commandList = device.BeginFrame();
    screen.BeginScene(commandList, device.BackBufferView());

    shapes.BeginFrame(device.FrameIndex());
    text.BeginFrame(device.FrameIndex());

    page.Draw(shapes, text);

    // Shapes first, then text, in two draw calls rather than interleaved. Painter's order still
    // holds within each pass, and the one place it matters across them -- a caption on a card --
    // is fine because every glyph is drawn after every rectangle.
    shapes.Flush(commandList);
    text.Flush(commandList);

    device.EndFrameAndPresent();
    device.DrainDebugMessages();
  }

  g_pointerInput = nullptr;

  // Drain the GPU here, not in ~Device. Destructors run in reverse declaration order, so the
  // SceneTarget's depth buffer would otherwise be released while the last submitted command list
  // still referenced it -- which the debug layer reports as OBJECT_DELETED_WHILE_STILL_IN_USE and
  // a release build turns into a use-after-free.
  device.WaitForGpu();
  device.DrainDebugMessages();

  return EXIT_SUCCESS;
}
} // namespace

int APIENTRY wWinMain(_In_ HINSTANCE _instance, _In_opt_ HINSTANCE _previousInstance, _In_ LPWSTR _commandLine, _In_ int _showCommand)
{
  UNREFERENCED_PARAMETER(_previousInstance);
  UNREFERENCED_PARAMETER(_commandLine);

  // Before the window, before anything. A process that is not DPI-aware gets its window
  // *bitmap-stretched* by Windows on a scaled display -- on a 125% desktop the 1280x720 client
  // this game asks for is blown up to 1600x900 by the compositor, with bilinear filtering. That
  // is precisely the resample Design/README.md section 1 rules out, and it is invisible from
  // inside the process: every D3D12 call still reports 1280x720 and every pixel we write is still
  // exact.
  //
  // Not an error check: a Windows build without the call is one where the manifest default
  // applies, and there is nothing useful to do about it here.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // Before the window too. With this on, a mouse click arrives as WM_POINTERDOWN exactly as a
  // finger does, so touch and mouse are one code path rather than two (MVP-01 section 2). If it
  // ever fails, the game gets no pointer messages from a mouse at all -- which is a thing to
  // report rather than to paper over with a WM_LBUTTONDOWN handler.
  if (!Neuron::PointerInput::EnableMouseAsPointer())
  {
    Neuron::DebugTrace("Input: EnableMouseInPointer failed; a mouse will not produce pointer messages.\n");
  }

  g_instance = _instance;

  if (!RegisterWindowClass(_instance))
  {
    return EXIT_FAILURE;
  }

  HWND window = CreateMainWindow(_instance, _showCommand);
  if (window == nullptr)
  {
    return EXIT_FAILURE;
  }

  // The composition root is the one place that catches. Debug.h routes every HRESULT that had to
  // succeed, every failed Win32 call and every broken invariant into one exception precisely so
  // that there is one place to tell a person about it.
  try
  {
    return RunGame(window);
  }
  catch (const winrt::hresult_error& error)
  {
    MessageBoxW(nullptr, error.message().c_str(), WINDOW_TITLE, MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
  }
  catch (const std::exception& error)
  {
    MessageBoxA(nullptr, error.what(), "Frontier Outpost", MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
  }
}
