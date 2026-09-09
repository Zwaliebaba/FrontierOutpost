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
#include "Palette.h"
#include "PaletteTarget.h"

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

LRESULT CALLBACK WndProc(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam)
{
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
HWND CreateMainWindow(HINSTANCE _instance, int _showCommand)
{
  constexpr DWORD STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

  RECT bounds = {0, 0, VIRTUAL_WIDTH * PRESENT_SCALE, VIRTUAL_HEIGHT * PRESENT_SCALE};
  AdjustWindowRect(&bounds, STYLE, FALSE);

  HWND window = CreateWindowExW(0, WINDOW_CLASS_NAME, WINDOW_TITLE, STYLE, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                                bounds.bottom - bounds.top, nullptr, nullptr, _instance, nullptr);
  if (window == nullptr)
  {
    return nullptr;
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
  device.Create(_window, VIRTUAL_WIDTH * PRESENT_SCALE, VIRTUAL_HEIGHT * PRESENT_SCALE);

  // One shader-visible descriptor heap for the whole client: only one can be bound at a time, so
  // every renderer allocates its slots out of this (DescriptorHeap.h).
  Neuron::DescriptorHeap shaderVisibleHeap;
  shaderVisibleHeap.Create(device.Handle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, true);

  Neuron::PaletteTarget screen;
  screen.Create(device.Handle(), shaderVisibleHeap, Neuron::ToIndex(Neuron::PaletteIndex::Blue));

  while (PumpMessages())
  {
    ID3D12GraphicsCommandList* commandList = device.BeginFrame();

    // Everything the game draws goes between BeginScene and Resolve, and every one of those
    // draws writes a palette index. There is nothing to draw yet, so the frame is the clear.
    screen.BeginScene(commandList);
    screen.Resolve(commandList, device.BackBufferView(), device.BackBufferWidthPixels(), device.BackBufferHeightPixels(), PRESENT_SCALE);

    device.EndFrameAndPresent();
    device.DrainDebugMessages();
  }

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
