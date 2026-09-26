// NeuronClient/Window.h
#pragma once

#include "Key.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace Neuron
{

enum class MouseButton : std::uint8_t
{
  Left,
  Right,
  Middle,
  X1,
  X2
};

/// A point in a window's client area, in pixels from its top left corner. It is outside the
/// client area while a button held down there has the mouse captured.
struct ClientPoint
{
  std::int32_t xPixels;
  std::int32_t yPixels;
};

/// A key went down, went down again by auto-repeat, or came up.
struct KeyEvent
{
  Key key;
  bool down;
  bool repeat;
};

/// A character the keyboard typed, surrogate pairs joined.
struct CharacterEvent
{
  char32_t codePoint;
};

struct MouseButtonEvent
{
  MouseButton button;
  bool down;
};

struct MouseMoveEvent
{
  ClientPoint position;
};

/// The vertical wheel turned: positive away from the user, in notches of 120 units.
struct MouseWheelEvent
{
  float notches;
};

struct FocusEvent
{
  bool gained;
};

/// The client area changed size: once a drag of the frame ends, or on a maximize or a restore,
/// never for a minimize.
struct ResizeEvent
{
  std::uint32_t widthPixels;
  std::uint32_t heightPixels;
};

using WindowEvent = std::variant<KeyEvent, CharacterEvent, MouseButtonEvent, MouseMoveEvent, MouseWheelEvent, FocusEvent, ResizeEvent>;

/// A Win32 window and its message pump (Design/ADR/ADR-012). Messages become WindowEvents, in the
/// order they arrive. WM_CLOSE, from the close button or Alt+F4, closes it; Alt and F10 do not open
/// the system menu. While a mouse button is held down over it, it captures the mouse. It is not
/// thread-safe: one thread opens it, pumps it and closes it.
class Window
{
public:
  struct Desc
  {
    std::string_view titleUtf8;
    std::uint32_t widthPixels; // of the client area
    std::uint32_t heightPixels;
    bool border;        // a title bar and a resizable frame, or a bare popup without them
    bool cursorVisible; // the OS cursor, over the window
  };

  /// Makes the process system-DPI-aware, then opens a window, shown and centered on the primary
  /// display, with a client area of the size asked for, even one larger than the display. On
  /// failure returns false and says why in _error.
  [[nodiscard]] static bool Open(const Desc& _desc, Window& _outWindow, std::string& _error);

  Window() noexcept;
  ~Window();
  Window(Window&& _other) noexcept;
  Window& operator=(Window&& _other) noexcept;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  /// True from Open until WM_CLOSE or Close.
  [[nodiscard]] bool IsOpen() const noexcept;

  /// Destroys the window.
  void Close() noexcept;

  /// The oldest event not yet returned. When there is none, the thread's pending messages are
  /// dispatched first. Returns false when there is still none.
  [[nodiscard]] bool PollEvent(WindowEvent& _outEvent);

  /// Where the cursor is now.
  [[nodiscard]] ClientPoint CursorPosition() const noexcept;

  /// The window's HWND, which a SwapChain presents to (ADR-012).
  [[nodiscard]] void* NativeHandle() const noexcept;

private:
  struct Native;

  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
