// NeuronClient/Key.cpp
#include "pch.h"

#include "Key.h"

#pragma comment(lib, "user32.lib")

namespace Neuron
{

namespace
{

constexpr std::uint32_t EXTENDED_KEY_BIT = 1u << 24;
constexpr std::uint32_t SCAN_CODE_SHIFT = 16;
constexpr std::uint32_t SCAN_CODE_MASK = 0xFF;

/// The key _offset places after _first, in a run of the enumeration that follows the virtual keys'
/// own order.
Key After(Key _first, std::uint32_t _offset) noexcept
{
  return static_cast<Key>(static_cast<std::uint32_t>(_first) + _offset);
}

} // namespace

Key KeyFromVirtualKey(std::uint32_t _virtualKey, std::uint32_t _keyData) noexcept
{
  if (_virtualKey >= 'A' && _virtualKey <= 'Z')
  {
    return After(Key::A, _virtualKey - 'A');
  }
  if (_virtualKey >= '0' && _virtualKey <= '9')
  {
    return After(Key::Digit0, _virtualKey - '0');
  }
  if (_virtualKey >= VK_NUMPAD0 && _virtualKey <= VK_NUMPAD9)
  {
    return After(Key::Numpad0, _virtualKey - VK_NUMPAD0);
  }
  if (_virtualKey >= VK_F1 && _virtualKey <= VK_F15)
  {
    return After(Key::F1, _virtualKey - VK_F1);
  }

  const bool extended = (_keyData & EXTENDED_KEY_BIT) != 0;
  switch (_virtualKey)
  {
  case VK_SHIFT:
    // Both Shift keys arrive as VK_SHIFT: the scan code tells them apart.
    return ((_keyData >> SCAN_CODE_SHIFT) & SCAN_CODE_MASK) == MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC) ? Key::LeftShift
                                                                                                          : Key::RightShift;
  case VK_LSHIFT:
    return Key::LeftShift;
  case VK_RSHIFT:
    return Key::RightShift;
  case VK_CONTROL:
    return extended ? Key::RightControl : Key::LeftControl;
  case VK_LCONTROL:
    return Key::LeftControl;
  case VK_RCONTROL:
    return Key::RightControl;
  case VK_MENU:
    return extended ? Key::RightAlt : Key::LeftAlt;
  case VK_LMENU:
    return Key::LeftAlt;
  case VK_RMENU:
    return Key::RightAlt;
  case VK_LWIN:
    return Key::LeftSystem;
  case VK_RWIN:
    return Key::RightSystem;
  case VK_APPS:
    return Key::Menu;
  case VK_ADD:
    return Key::NumpadAdd;
  case VK_SUBTRACT:
    return Key::NumpadSubtract;
  case VK_MULTIPLY:
    return Key::NumpadMultiply;
  case VK_DIVIDE:
    return Key::NumpadDivide;
  case VK_ESCAPE:
    return Key::Escape;
  case VK_TAB:
    return Key::Tab;
  case VK_BACK:
    return Key::Backspace;
  case VK_RETURN:
    return Key::Enter;
  case VK_SPACE:
    return Key::Space;
  case VK_INSERT:
    return Key::Insert;
  case VK_DELETE:
    return Key::Delete;
  case VK_HOME:
    return Key::Home;
  case VK_END:
    return Key::End;
  case VK_PRIOR:
    return Key::PageUp;
  case VK_NEXT:
    return Key::PageDown;
  case VK_LEFT:
    return Key::Left;
  case VK_RIGHT:
    return Key::Right;
  case VK_UP:
    return Key::Up;
  case VK_DOWN:
    return Key::Down;
  case VK_OEM_3:
    return Key::Grave;
  case VK_OEM_MINUS:
    return Key::Minus;
  case VK_OEM_PLUS:
    return Key::Equal;
  case VK_OEM_4:
    return Key::LeftBracket;
  case VK_OEM_6:
    return Key::RightBracket;
  case VK_OEM_5:
    return Key::Backslash;
  case VK_OEM_1:
    return Key::Semicolon;
  case VK_OEM_7:
    return Key::Apostrophe;
  case VK_OEM_COMMA:
    return Key::Comma;
  case VK_OEM_PERIOD:
    return Key::Period;
  case VK_OEM_2:
    return Key::Slash;
  case VK_PAUSE:
    return Key::Pause;
  default:
    return Key::Unknown;
  }
}

} // namespace Neuron
