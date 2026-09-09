// FrontierOutpost.cpp -- process entry point. Hosts the client and the server in one process
// (the server half is not wired up yet).
//
// This is the wizard's wWinMain reduced to what the game actually needs: one fixed-size,
// non-resizable window, no menu and no About dialog. The window is the presentation target
// described in Design/README.md -- a 640x400 framebuffer scaled by an integer factor, so the
// client area is exactly SCALE times the virtual resolution and every virtual pixel lands on a
// whole number of physical ones. The D3D12 device, the 16-colour palette and the game loop
// replace the GetMessage loop below; nothing here is meant to survive that.

#include "pch.h"
#include "FrontierOutpost.h"

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

LRESULT CALLBACK WndProc(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam)
{
  switch (_message)
  {
  case WM_PAINT:
  {
    PAINTSTRUCT paint;
    BeginPaint(_window, &paint);
    EndPaint(_window, &paint);
    return 0;
  }

  case WM_DESTROY:
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
  //windowClass.hIcon = LoadIconW(_instance, MAKEINTRESOURCEW(IDI_FRONTIEROUTPOST));
  //windowClass.hIconSm = LoadIconW(_instance, MAKEINTRESOURCEW(IDI_SMALL));
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

  if (CreateMainWindow(_instance, _showCommand) == nullptr)
  {
    return EXIT_FAILURE;
  }

  MSG message = {};
  while (GetMessageW(&message, nullptr, 0, 0) > 0)
  {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  return static_cast<int>(message.wParam);
}
