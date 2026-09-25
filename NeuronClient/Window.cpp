// NeuronClient/Window.cpp
#include "pch.h"

#include "Unicode.h"
#include "Window.h"

#include <deque>
#include <format>
#include <utility>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace Neuron
{

namespace
{

constexpr const wchar_t* CLASS_NAME = L"NeuronClient.Window";

/// The window property that holds the window's Native: a HANDLE, so that no integer becomes a
/// pointer on the way back.
constexpr const wchar_t* NATIVE_PROPERTY = L"NeuronClient.Window.Native";

/// Windows keeps a window no larger than the desktop unless it is told otherwise, and the client
/// area asked for, with its frame, may be larger.
constexpr LONG MAX_TRACK_PIXELS = 50000;

constexpr float WHEEL_NOTCH = static_cast<float>(WHEEL_DELTA);

constexpr char32_t HIGH_SURROGATE_FIRST = 0xD800;
constexpr char32_t LOW_SURROGATE_FIRST = 0xDC00;
constexpr char32_t LOW_SURROGATE_LAST = 0xDFFF;
constexpr char32_t REPLACEMENT_CHARACTER = 0xFFFD;

std::int32_t LowSigned(LPARAM _value) noexcept
{
  return static_cast<std::int16_t>(LOWORD(_value));
}

std::int32_t HighSigned(LPARAM _value) noexcept
{
  return static_cast<std::int16_t>(HIWORD(_value));
}

} // namespace

struct Window::Native
{
  HWND handle = nullptr;
  std::deque<WindowEvent> events;
  std::uint32_t widthPixels = 0; // as last reported
  std::uint32_t heightPixels = 0;
  char32_t highSurrogate = 0;
  bool open = false;
  bool resizing = false;
  bool cursorHidden = false;

  Native() = default;
  Native(const Native&) = delete;
  Native& operator=(const Native&) = delete;

  ~Native()
  {
    Destroy();
  }

  /// An exception cannot unwind through Windows, so one here ends the program. The property is set
  /// once the window is made: what arrives before is Windows' alone, as it was SFML's.
  static LRESULT CALLBACK Procedure(HWND _handle, UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept
  {
    auto* native = static_cast<Native*>(GetPropW(_handle, NATIVE_PROPERTY));
    LRESULT result = 0;
    if (native != nullptr && native->Handle(_message, _wParam, _lParam, result))
    {
      return result;
    }
    return DefWindowProcW(_handle, _message, _wParam, _lParam);
  }

  /// Queues what a message says. Returns true when it has answered the message with _outResult,
  /// and false when Windows is to answer it.
  bool Handle(UINT _message, WPARAM _wParam, LPARAM _lParam, LRESULT& _outResult)
  {
    switch (_message)
    {
    case WM_CLOSE:
      // The close button and Alt+F4 close the window (ADR-012). The window stays until Close.
      open = false;
      _outResult = 0;
      return true;
    case WM_SYSCOMMAND:
      // Alt and F10 do not open the system menu, which would take the keyboard away.
      if ((_wParam & 0xFFF0) == SC_KEYMENU)
      {
        _outResult = 0;
        return true;
      }
      return false;
    case WM_GETMINMAXINFO:
      // Windows passes the MINMAXINFO to fill in as the message's LPARAM: there is no other route
      // to it, so this one integer does become a pointer.
      // NOLINTNEXTLINE(performance-no-int-to-ptr)
      reinterpret_cast<MINMAXINFO*>(_lParam)->ptMaxTrackSize = {MAX_TRACK_PIXELS, MAX_TRACK_PIXELS};
      _outResult = 0;
      return true;
    case WM_SIZE:
      if (_wParam != SIZE_MINIMIZED && !resizing)
      {
        ReportSize();
      }
      return false;
    case WM_ENTERSIZEMOVE:
      resizing = true;
      return false;
    case WM_EXITSIZEMOVE:
      resizing = false;
      ReportSize();
      return false;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      events.emplace_back(FocusEvent{_message == WM_SETFOCUS});
      return false;
    case WM_SETCURSOR:
      if (LOWORD(_lParam) == HTCLIENT)
      {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        _outResult = TRUE;
        return true;
      }
      return false;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
      const bool down = _message == WM_KEYDOWN || _message == WM_SYSKEYDOWN;
      const bool repeat = down && (HIWORD(_lParam) & KF_REPEAT) != 0;
      events.emplace_back(
        KeyEvent{KeyFromVirtualKey(static_cast<std::uint32_t>(_wParam), static_cast<std::uint32_t>(_lParam)), down, repeat});
      // Windows goes on with it, so that Alt+F4 still closes the window.
      return false;
    }
    case WM_CHAR:
      Character(static_cast<char32_t>(_wParam));
      return false;
    case WM_MOUSEWHEEL:
      events.emplace_back(MouseWheelEvent{static_cast<float>(GET_WHEEL_DELTA_WPARAM(_wParam)) / WHEEL_NOTCH});
      return false;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
      events.emplace_back(MouseButtonEvent{MouseButton::Left, _message == WM_LBUTTONDOWN});
      return false;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
      events.emplace_back(MouseButtonEvent{MouseButton::Right, _message == WM_RBUTTONDOWN});
      return false;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
      events.emplace_back(MouseButtonEvent{MouseButton::Middle, _message == WM_MBUTTONDOWN});
      return false;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
      events.emplace_back(
        MouseButtonEvent{GET_XBUTTON_WPARAM(_wParam) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2, _message == WM_XBUTTONDOWN});
      return false;
    case WM_MOUSEMOVE:
    {
      // While a button is held, the window keeps the mouse, so that a drag past its edge is seen.
      const bool held = (_wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)) != 0;
      if (!held && GetCapture() == handle)
      {
        ReleaseCapture();
      }
      else if (held && GetCapture() != handle)
      {
        SetCapture(handle);
      }
      events.emplace_back(MouseMoveEvent{{LowSigned(_lParam), HighSigned(_lParam)}});
      return false;
    }
    default:
      return false;
    }
  }

  /// A UTF-16 unit of what was typed. A pair of surrogates makes one character; a surrogate on its
  /// own is U+FFFD.
  void Character(char32_t _unit)
  {
    if (_unit >= HIGH_SURROGATE_FIRST && _unit < LOW_SURROGATE_FIRST)
    {
      highSurrogate = _unit;
      return;
    }
    char32_t codePoint = _unit;
    if (_unit >= LOW_SURROGATE_FIRST && _unit <= LOW_SURROGATE_LAST)
    {
      codePoint = highSurrogate != 0
                    ? static_cast<char32_t>(0x10000 + ((highSurrogate - HIGH_SURROGATE_FIRST) << 10) + (_unit - LOW_SURROGATE_FIRST))
                    : REPLACEMENT_CHARACTER;
      highSurrogate = 0;
    }
    events.emplace_back(CharacterEvent{codePoint});
  }

  /// Queues a ResizeEvent when the client area is no longer the size last reported.
  void ReportSize()
  {
    RECT client{};
    GetClientRect(handle, &client);
    const auto width = static_cast<std::uint32_t>(client.right - client.left);
    const auto height = static_cast<std::uint32_t>(client.bottom - client.top);
    if (width != widthPixels || height != heightPixels)
    {
      widthPixels = width;
      heightPixels = height;
      events.emplace_back(ResizeEvent{width, height});
    }
  }

  void Destroy() noexcept
  {
    open = false;
    if (handle != nullptr)
    {
      const HWND closing = handle;
      handle = nullptr;
      RemovePropW(closing, NATIVE_PROPERTY);
      if (GetCapture() == closing)
      {
        ReleaseCapture();
      }
      DestroyWindow(closing);
    }
    if (cursorHidden)
    {
      ShowCursor(TRUE);
      cursorHidden = false;
    }
  }
};

bool Window::Open(const Desc& _desc, Window& _outWindow, std::string& _error)
{
  // System DPI awareness, as SFML set it (ADR-012). A process that has an awareness keeps it.
  SetProcessDPIAware();

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_OWNDC; // a device context of its own, as OpenGL wants
  windowClass.lpfnWndProc = &Native::Procedure;
  windowClass.hInstance = instance;
  windowClass.lpszClassName = CLASS_NAME;
  if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
  {
    _error = std::format("Window: RegisterClassExW failed with error {}", GetLastError());
    return false;
  }

  const DWORD style = WS_VISIBLE | (_desc.border ? WS_CAPTION | WS_MINIMIZEBOX | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_SYSMENU : WS_POPUP);

  // Centered by the client area's size, then grown by the frame, where SFML placed it.
  const HDC screen = GetDC(nullptr);
  const int left = (GetDeviceCaps(screen, HORZRES) - static_cast<int>(_desc.widthPixels)) / 2;
  const int top = (GetDeviceCaps(screen, VERTRES) - static_cast<int>(_desc.heightPixels)) / 2;
  ReleaseDC(nullptr, screen);
  RECT frame{0, 0, static_cast<LONG>(_desc.widthPixels), static_cast<LONG>(_desc.heightPixels)};
  AdjustWindowRect(&frame, style, FALSE);
  const int width = frame.right - frame.left;
  const int height = frame.bottom - frame.top;

  auto native = std::make_unique<Native>();
  native->widthPixels = _desc.widthPixels;
  native->heightPixels = _desc.heightPixels;
  const std::wstring title = Utf8ToUtf16(_desc.titleUtf8);
  const HWND handle = CreateWindowExW(0, CLASS_NAME, title.c_str(), style, left, top, width, height, nullptr, nullptr, instance, nullptr);
  if (handle == nullptr)
  {
    _error = std::format("Window: CreateWindowExW failed with error {}", GetLastError());
    return false;
  }
  native->handle = handle;
  if (SetPropW(handle, NATIVE_PROPERTY, native.get()) == 0)
  {
    _error = std::format("Window: SetPropW failed with error {}", GetLastError());
    return false;
  }
  native->open = true;

  // Windows made the window no larger than the desktop. Now that WM_GETMINMAXINFO is answered, it
  // takes the size asked for.
  SetWindowPos(handle, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);

  if (!_desc.cursorVisible)
  {
    ShowCursor(FALSE);
    native->cursorHidden = true;
  }
  _outWindow.m_native = std::move(native);
  return true;
}

Window::Window() noexcept = default;
Window::~Window() = default;
Window::Window(Window&&) noexcept = default;
Window& Window::operator=(Window&&) noexcept = default;

bool Window::IsOpen() const noexcept
{
  return m_native && m_native->open;
}

void Window::Close() noexcept
{
  if (m_native)
  {
    m_native->Destroy();
  }
}

bool Window::PollEvent(WindowEvent& _outEvent)
{
  if (!m_native)
  {
    return false;
  }
  if (m_native->events.empty())
  {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0)
    {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  if (m_native->events.empty())
  {
    return false;
  }
  _outEvent = m_native->events.front();
  m_native->events.pop_front();
  return true;
}

ClientPoint Window::CursorPosition() const noexcept
{
  POINT point{};
  if (m_native && m_native->handle != nullptr && GetCursorPos(&point) != 0)
  {
    ScreenToClient(m_native->handle, &point);
  }
  return {point.x, point.y};
}

void* Window::NativeHandle() const noexcept
{
  return m_native ? m_native->handle : nullptr;
}

} // namespace Neuron
