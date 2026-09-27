// Tests/NeuronClientTests/WindowEvents.cpp
//
// Neuron::Window opens at the client size asked for, turns the messages sent to it into events in
// their order, and closes on WM_CLOSE without Windows destroying it (Design/ADR/ADR-012). Fullscreen,
// it is borderless over the primary display (Design/ADR/ADR-017).
#include "pch.h"

#include "Check.h"
#include "Window.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

constexpr std::uint32_t WIDTH_PIXELS = 320;
constexpr std::uint32_t HEIGHT_PIXELS = 200;
constexpr LPARAM REPEATED = LPARAM{1} << 30;

Neuron::Window Open(bool _fullscreen = false)
{
  Neuron::Window window;
  std::string error;
  Assert::IsTrue(Neuron::Window::Open({.titleUtf8 = "NeuronClientTests",
                                       .widthPixels = WIDTH_PIXELS,
                                       .heightPixels = HEIGHT_PIXELS,
                                       .border = true,
                                       .cursorVisible = true,
                                       .fullscreen = _fullscreen},
                                      window, error),
                 Widen(error).c_str());
  return window;
}

HWND Handle(const Neuron::Window& _window)
{
  return static_cast<HWND>(_window.NativeHandle());
}

/// The whole of the primary display, which fullscreen covers.
RECT PrimaryDisplay()
{
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  Assert::IsTrue(GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) != 0);
  return monitor.rcMonitor;
}

void AssertClientSize(const Neuron::Window& _window, LONG _widthPixels, LONG _heightPixels)
{
  RECT client{};
  Assert::IsTrue(GetClientRect(Handle(_window), &client) != 0);
  Assert::AreEqual(static_cast<int>(_widthPixels), static_cast<int>(client.right), L"client width");
  Assert::AreEqual(static_cast<int>(_heightPixels), static_cast<int>(client.bottom), L"client height");
  Assert::AreEqual(static_cast<std::uint32_t>(_widthPixels), _window.WidthPixels(), L"reported width");
  Assert::AreEqual(static_cast<std::uint32_t>(_heightPixels), _window.HeightPixels(), L"reported height");
}

/// The events of one kind the window has, once the thread's messages are dispatched. Windows may
/// add others of its own, such as a move when the window appears under the cursor.
template <typename T> std::vector<T> Drain(Neuron::Window& _window)
{
  std::vector<T> events;
  Neuron::WindowEvent event;
  while (_window.PollEvent(event))
  {
    if (const T* wanted = std::get_if<T>(&event))
    {
      events.push_back(*wanted);
    }
  }
  return events;
}

/// Drains what the window has, so that a test sees only the events its own messages make.
void Settle(Neuron::Window& _window)
{
  Neuron::WindowEvent event;
  while (_window.PollEvent(event))
  {
  }
}

template <typename T> T Only(Neuron::Window& _window)
{
  const std::vector<T> events = Drain<T>(_window);
  Assert::AreEqual(std::size_t{1}, events.size(), L"one message, one event");
  return events.front();
}

} // namespace

TEST_CLASS(WindowEvents)
{
public:
  TEST_METHOD(OpensWithTheClientAreaAskedFor)
  {
    Neuron::Window window = Open();
    Assert::IsTrue(window.IsOpen());
    RECT client{};
    Assert::IsTrue(GetClientRect(Handle(window), &client) != 0);
    Assert::AreEqual(static_cast<int>(WIDTH_PIXELS), static_cast<int>(client.right));
    Assert::AreEqual(static_cast<int>(HEIGHT_PIXELS), static_cast<int>(client.bottom));
  }

  TEST_METHOD(TurnsKeyMessagesIntoKeyEvents)
  {
    Neuron::Window window = Open();
    Settle(window);
    const LPARAM scanCodeA = static_cast<LPARAM>(MapVirtualKeyW('A', MAPVK_VK_TO_VSC)) << 16;
    SendMessageW(Handle(window), WM_KEYDOWN, 'A', scanCodeA);
    SendMessageW(Handle(window), WM_KEYDOWN, 'A', scanCodeA | REPEATED);
    SendMessageW(Handle(window), WM_KEYUP, 'A', scanCodeA | REPEATED);
    const std::vector<Neuron::KeyEvent> events = Drain<Neuron::KeyEvent>(window);
    Assert::AreEqual(std::size_t{3}, events.size());
    const Neuron::KeyEvent& first = events[0];
    const Neuron::KeyEvent& again = events[1];
    const Neuron::KeyEvent& up = events[2];
    Assert::IsTrue(first.key == Neuron::Key::A && first.down && !first.repeat, L"the first press");
    Assert::IsTrue(again.key == Neuron::Key::A && again.down && again.repeat, L"the auto-repeat");
    Assert::IsTrue(up.key == Neuron::Key::A && !up.down && !up.repeat, L"the release");
  }

  TEST_METHOD(JoinsSurrogatePairsIntoOneCharacter)
  {
    Neuron::Window window = Open();
    Settle(window);
    SendMessageW(Handle(window), WM_CHAR, 0xD83D, 0);
    SendMessageW(Handle(window), WM_CHAR, 0xDE00, 0);
    Assert::AreEqual(0x1F600u, static_cast<unsigned>(Only<Neuron::CharacterEvent>(window).codePoint));
    SendMessageW(Handle(window), WM_CHAR, 'x', 0);
    Assert::AreEqual(static_cast<unsigned>('x'), static_cast<unsigned>(Only<Neuron::CharacterEvent>(window).codePoint));
  }

  TEST_METHOD(CountsWheelNotches)
  {
    Neuron::Window window = Open();
    Settle(window);
    SendMessageW(Handle(window), WM_MOUSEWHEEL, MAKEWPARAM(0, 2 * WHEEL_DELTA), 0);
    Assert::AreEqual(2.0f, Only<Neuron::MouseWheelEvent>(window).notches);
    SendMessageW(Handle(window), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA / 2)), 0);
    Assert::AreEqual(-0.5f, Only<Neuron::MouseWheelEvent>(window).notches);
  }

  TEST_METHOD(ReportsButtonsAndMoves)
  {
    Neuron::Window window = Open();
    Settle(window);
    SendMessageW(Handle(window), WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), 0);
    const Neuron::MouseButtonEvent button = Only<Neuron::MouseButtonEvent>(window);
    Assert::IsTrue(button.button == Neuron::MouseButton::X2 && button.down, L"the second X button went down");
    SendMessageW(Handle(window), WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<WORD>(-5), 17));
    // Windows may add a move of its own, from where the cursor really is.
    bool moved = false;
    for (const Neuron::MouseMoveEvent& move : Drain<Neuron::MouseMoveEvent>(window))
    {
      moved = moved || (move.position.xPixels == -5 && move.position.yPixels == 17);
    }
    Assert::IsTrue(moved, L"no move to (-5, 17), left of the client area");
  }

  TEST_METHOD(OpensFullscreenOverThePrimaryDisplayWithoutAFrame)
  {
    Neuron::Window window = Open(true);
    Assert::IsTrue(IsWindowVisible(Handle(window)) != 0);
    Assert::IsTrue((GetWindowLongPtrW(Handle(window), GWL_STYLE) & WS_CAPTION) == 0, L"a frame while fullscreen");
    const RECT display = PrimaryDisplay();
    RECT placed{};
    Assert::IsTrue(GetWindowRect(Handle(window), &placed) != 0);
    Assert::IsTrue(EqualRect(&placed, &display) != 0, L"not over the whole of the primary display");
    AssertClientSize(window, display.right - display.left, display.bottom - display.top);
  }

  TEST_METHOD(ReportsTheClientSizeItOpensAt)
  {
    Neuron::Window window = Open();
    AssertClientSize(window, static_cast<LONG>(WIDTH_PIXELS), static_cast<LONG>(HEIGHT_PIXELS));
  }

  TEST_METHOD(ClosesOnWmCloseAndStaysUntilClose)
  {
    Neuron::Window window = Open();
    const HWND handle = Handle(window);
    SendMessageW(handle, WM_CLOSE, 0, 0);
    Assert::IsFalse(window.IsOpen());
    Assert::IsTrue(IsWindow(handle) != 0, L"Windows destroyed the window itself");
    window.Close();
    Assert::IsFalse(IsWindow(handle) != 0);
    Assert::IsTrue(window.NativeHandle() == nullptr);
  }
};

} // namespace NeuronClientTests
