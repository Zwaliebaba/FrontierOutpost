// FrontierOutpost.cpp -- process entry point. Hosts the client and the server in one process
// (the server half is not wired up yet).
//
// This is the wizard's wWinMain reduced to what the game actually needs: one fixed-size,
// non-resizable window, no menu and no About dialog. The window is the presentation target
// described in Design/README.md -- a 640x400 framebuffer scaled by an integer factor, so the
// client area is exactly SCALE times the virtual resolution and every virtual pixel lands on a
// whole number of physical ones.
//
// There is no WM_PAINT handler and there must not be one: from the moment the swap chain exists
// it owns every pixel of the client area, and a BeginPaint/EndPaint pair racing it produces a
// flash and nothing else (AGENTS.md 4, NOGDI).

#include "pch.h"
#include "FrontierOutpost.h"

#include "Device.h"
#include "FontRenderer.h"
#include "IsometricCamera.h"
#include "LoopbackTransport.h"
#include "MeshRenderer.h"
#include "Palette.h"
#include "PaletteTarget.h"
#include "PointerInput.h"
#include "Session.h"

#include "ShipMesh.h"
#include "ShipView.h"
#include "StationMesh.h"
#include "World.h"

#include <chrono>

namespace
{

// The legacy screen the game presents, and the integer factor it is blown up by. Both are
// deliberately compile-time: a non-integer scale is what turns crisp 640x400 into mush.
constexpr int VIRTUAL_WIDTH = 640;
constexpr int VIRTUAL_HEIGHT = 400;
constexpr int PRESENT_SCALE = 2;

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

// Sizes for the CLIENT area, not the window: AdjustWindowRect adds the border and caption, so
// the framebuffer is presented 1:1 at PRESENT_SCALE rather than a few rows short of it.
//
// AdjustWindowRect assumes 96 DPI, and under per-monitor awareness the caption on a scaled
// display is not 96 DPI, so its answer is close rather than right. Rather than reach for
// AdjustWindowRectExForDpi and a DPI to pass it, the window is created and then measured: if the
// client area came out anything other than exact, the difference is added back. That is correct
// on every DPI, theme and Windows version without knowing anything about any of them -- and this
// has to be exact, because a client area one row short means the bottom row of virtual pixels is
// not PRESENT_SCALE physical pixels tall.
HWND CreateMainWindow(HINSTANCE _instance, int _showCommand)
{
  constexpr DWORD STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  constexpr int CLIENT_WIDTH = VIRTUAL_WIDTH * PRESENT_SCALE;
  constexpr int CLIENT_HEIGHT = VIRTUAL_HEIGHT * PRESENT_SCALE;

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

/// The status line: the tick the server is on and where it says the ship is.
///
/// Metres to one decimal rather than millimetres, because millimetres on a screen where one pixel
/// is 125 mm is four digits of noise. The tick is what makes it possible to see at a glance that
/// the server is running at all.
std::string StatusLine(const Frontier::ShipView& _ship)
{
  if (!_ship.HasState())
  {
    return "TICK ----  WAITING FOR SERVER";
  }

  return std::format("TICK {:<6} X {:>8.1F} Z {:>8.1F}", _ship.Tick(), _ship.PositionXMetres(), _ship.PositionZMetres());
}

int RunGame(HWND _window)
{
  Neuron::Device device;
  device.Create(_window, VIRTUAL_WIDTH * PRESENT_SCALE, VIRTUAL_HEIGHT * PRESENT_SCALE);

  // One shader-visible descriptor heap for the whole client: only one can be bound at a time, so
  // every renderer allocates its slots out of this (DescriptorHeap.h).
  Neuron::DescriptorHeap shaderVisibleHeap;
  shaderVisibleHeap.Create(device.Handle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, true);

  // Space is black. The blue of step 2 was there to prove the palette; from here the clear index
  // is the color of empty space.
  Neuron::PaletteTarget screen;
  screen.Create(device.Handle(), shaderVisibleHeap, Neuron::ToIndex(Neuron::PaletteIndex::Black));

  Neuron::FontRenderer text;
  text.Create(device, shaderVisibleHeap);

  // One renderer, many meshes: the pipeline is how meshes are drawn and the buffers are which
  // mesh (Mesh.h).
  Neuron::MeshRenderer meshRenderer;
  meshRenderer.Create(device.Handle());

  Neuron::Mesh shipMesh;
  shipMesh.Create(device.Handle(), Frontier::SHIP_VERTICES, Frontier::SHIP_INDICES);

  Neuron::Mesh stationMesh;
  stationMesh.Create(device.Handle(), Frontier::STATION_VERTICES, Frontier::STATION_INDICES);

  // The station never moves, so its world matrix is built once rather than every frame.
  const std::array<float, 16> stationWorld = Neuron::WorldMatrix(0.0F, Frontier::STATION_POSITION_X, 0.0F, Frontier::STATION_POSITION_Z);

  Neuron::IsometricCamera camera{static_cast<float>(VIRTUAL_WIDTH), static_cast<float>(VIRTUAL_HEIGHT)};

  // The server. It gets its own thread here and keeps it: same-thread is not a stage this passes
  // through (MVP-01 section 2). The transport outlives the session, which is why it is declared
  // first -- destructors run in reverse, so the session stops before the queues it is using go.
  Neuron::LoopbackTransport transport;
  Neuron::Session session;
  session.Start(std::make_unique<Frontier::World>(), transport);

  // The client's entire opinion about where the ship is. It cannot move it; only a state arriving
  // from the transport changes what this says (ADR-005).
  Frontier::ShipView ship;

  Neuron::PointerInput pointer;
  pointer.Create(_window, PRESENT_SCALE);
  g_pointerInput = &pointer;

  auto previousFrame = std::chrono::steady_clock::now();

  while (PumpMessages())
  {
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>{now - previousFrame}.count();
    previousFrame = now;

    // Drain every state that arrived since the last frame. At 20 Hz against a display running
    // faster, this is usually none or one.
    Neuron::ShipState state = {};
    while (transport.ReceiveState(state))
    {
      ship.Accept(state);
    }
    ship.Advance(elapsedSeconds);

    // Before the first state has arrived there is no ship, so the camera sits at the origin and
    // nothing is drawn. It lasts one tick at most and it is the honest thing to show: the client
    // has not been told where anything is (ADR-005).
    const Neuron::IsometricCamera::WorldPoint shipPosition = {ship.PositionXMetres(), 0.0F, ship.PositionZMetres()};
    camera.Follow(ship.HasState() ? shipPosition : Neuron::IsometricCamera::WorldPoint{0.0F, 0.0F, 0.0F});

    // The click, and the whole of what the client does with it: un-project it onto the ground and
    // hand the world point to the server. The client does not move the ship, does not predict
    // where it will go and does not remember where it was told to go -- it sends an order and
    // waits to be told (MVP-01 section 2).
    //
    // The camera is followed BEFORE this, so the un-projection uses the same camera the frame is
    // about to be drawn with. Doing it after would answer with the previous frame's camera, which
    // is a whole tick of the ship's travel out at speed.
    float clickXTexels = 0.0F;
    float clickYTexels = 0.0F;
    if (pointer.TakeClick(clickXTexels, clickYTexels))
    {
      const Neuron::IsometricCamera::WorldPoint target = camera.UnprojectToGround(clickXTexels, clickYTexels);
      transport.SendOrder(Neuron::MoveToOrder{
        .targetXMillimetres = static_cast<std::int64_t>(std::lround(target.x * 1000.0F)),
        .targetZMillimetres = static_cast<std::int64_t>(std::lround(target.z * 1000.0F)),
      });
    }

    ID3D12GraphicsCommandList* commandList = device.BeginFrame();

    // Everything the game draws goes between BeginScene and Resolve, and every one of those
    // draws writes a palette index.
    screen.BeginScene(commandList);

    // The station is drawn whether or not the server has spoken: it is not replicated state, it
    // is scenery, and it is in the same place every frame.
    meshRenderer.Draw(commandList, stationMesh, camera, stationWorld);

    if (ship.HasState())
    {
      meshRenderer.Draw(commandList, shipMesh, camera,
                        Neuron::WorldMatrix(ship.HeadingRadians(), shipPosition.x, shipPosition.y, shipPosition.z));
    }

    text.BeginFrame(device.FrameIndex());
    text.DrawText(8, 8, "FRONTIER OUTPOST", Neuron::ToIndex(Neuron::PaletteIndex::White));
    text.DrawText(8, 20, StatusLine(ship), Neuron::ToIndex(Neuron::PaletteIndex::BrightGreen));
    text.Flush(commandList);

    screen.Resolve(commandList, device.BackBufferView(), device.BackBufferWidthPixels(), device.BackBufferHeightPixels(), PRESENT_SCALE);

    device.EndFrameAndPresent();
    device.DrainDebugMessages();
  }

  // Before the GPU wait, so the server thread is not still pushing states into a transport that
  // is about to go out of scope.
  session.Stop();
  g_pointerInput = nullptr;

  // Drain the GPU here, not in ~Device. Destructors run in reverse declaration order, so the
  // PaletteTarget's index target and depth buffer would otherwise be released while the last
  // submitted command list still referenced them -- which the debug layer reports as
  // OBJECT_DELETED_WHILE_STILL_IN_USE and a release build turns into a use-after-free.
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
  // *bitmap-stretched* by Windows on a scaled display -- on a 125% desktop the 1280x800 client
  // this game asks for is blown up to 1600x1000 by the compositor, with bilinear filtering, on
  // top of the integer 2x scale the renderer was so careful about. That is precisely the
  // fractional scale Design/README.md section 1 rules out, and it is invisible from inside the
  // process: every D3D12 call still reports 1280x800 and every pixel we write is still exact.
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
