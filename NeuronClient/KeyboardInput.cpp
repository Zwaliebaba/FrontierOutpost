// KeyboardInput.cpp -- the keyboard, for the one screen that types.

#include "pch.h"
#include "KeyboardInput.h"

#include "TextField.h"

namespace Neuron
{

void KeyboardInput::Remember(Key _key) noexcept
{
  if (m_keyCount < m_keys.size())
  {
    m_keys[m_keyCount] = _key;
    ++m_keyCount;
  }
}

bool KeyboardInput::HandleMessage(std::uint32_t _message, std::uint64_t _wParam, std::int64_t _lParam) noexcept
{
  (void)_lParam;

  switch (_message)
  {
  case WM_CHAR:
  {
    // Already through the keyboard layout, so this is the character its owner meant to type.
    //
    // Control codes arrive here too -- Return is 13, Backspace is 8, Escape is 27 -- and they are
    // dropped, because they reach the field as named keys from WM_KEYDOWN below. Taking both would
    // backspace twice.
    const auto typed = static_cast<char>(_wParam);
    if (typed >= TextField::FIRST_PRINTABLE && typed <= TextField::LAST_PRINTABLE && m_typedCount < m_typed.size())
    {
      m_typed[m_typedCount] = typed;
      ++m_typedCount;
    }
    return true;
  }

  case WM_KEYDOWN:
  {
    switch (_wParam)
    {
    case VK_BACK:
      Remember(Key::Backspace);
      return true;
    case VK_DELETE:
      Remember(Key::Delete);
      return true;
    case VK_LEFT:
      Remember(Key::Left);
      return true;
    case VK_RIGHT:
      Remember(Key::Right);
      return true;
    case VK_HOME:
      Remember(Key::Home);
      return true;
    case VK_END:
      Remember(Key::End);
      return true;
    case VK_RETURN:
      Remember(Key::Enter);
      return true;
    case VK_TAB:
      Remember(Key::Tab);
      return true;
    case VK_ESCAPE:
      Remember(Key::Escape);
      return true;
    default:
      // Not a key this game uses. Left unconsumed so the window procedure can do whatever it
      // would have done -- Alt+F4 is not this class's business to swallow.
      return false;
    }
  }

  default:
    return false;
  }
}

std::string KeyboardInput::TakeTyped()
{
  // The allocation lives here rather than in the callback: this is called from the frame loop,
  // where throwing is somebody's to catch.
  std::string taken{m_typed.data(), m_typedCount};
  m_typedCount = 0;
  return taken;
}

std::vector<KeyboardInput::Key> KeyboardInput::TakeKeys()
{
  std::vector<Key> taken{m_keys.begin(), m_keys.begin() + static_cast<std::ptrdiff_t>(m_keyCount)};
  m_keyCount = 0;
  return taken;
}

} // namespace Neuron
