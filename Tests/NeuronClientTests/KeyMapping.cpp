// Tests/NeuronClientTests/KeyMapping.cpp
//
// Neuron::KeyFromVirtualKey names keys by position, tells the left modifiers from the right ones,
// and reaches every one of its 101 keys (Design/ADR/ADR-012).
#include "pch.h"

#include "Key.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

constexpr std::uint32_t EXTENDED = 1u << 24;
constexpr int KEY_COUNT = 101;

std::uint32_t ScanCodeOf(std::uint32_t _virtualKey)
{
  return MapVirtualKeyW(_virtualKey, MAPVK_VK_TO_VSC) << 16;
}

void AssertKey(Neuron::Key _expected, std::uint32_t _virtualKey, std::uint32_t _keyData = 0)
{
  const Neuron::Key key = Neuron::KeyFromVirtualKey(_virtualKey, _keyData);
  const std::wstring message = L"virtual key " + std::to_wstring(_virtualKey) + L" is key " + std::to_wstring(static_cast<int>(key));
  Assert::IsTrue(key == _expected, message.c_str());
}

} // namespace

TEST_CLASS(KeyMapping)
{
public:
  TEST_METHOD(MapsTheRunsOfLettersDigitsAndFunctionKeys)
  {
    AssertKey(Neuron::Key::A, 'A');
    AssertKey(Neuron::Key::Z, 'Z');
    AssertKey(Neuron::Key::Digit0, '0');
    AssertKey(Neuron::Key::Digit9, '9');
    AssertKey(Neuron::Key::Numpad0, VK_NUMPAD0);
    AssertKey(Neuron::Key::Numpad9, VK_NUMPAD9);
    AssertKey(Neuron::Key::F1, VK_F1);
    AssertKey(Neuron::Key::F15, VK_F15);
  }

  TEST_METHOD(TellsLeftModifiersFromRightOnes)
  {
    AssertKey(Neuron::Key::LeftShift, VK_SHIFT, ScanCodeOf(VK_LSHIFT));
    AssertKey(Neuron::Key::RightShift, VK_SHIFT, ScanCodeOf(VK_RSHIFT));
    AssertKey(Neuron::Key::LeftControl, VK_CONTROL);
    AssertKey(Neuron::Key::RightControl, VK_CONTROL, EXTENDED);
    AssertKey(Neuron::Key::LeftAlt, VK_MENU);
    AssertKey(Neuron::Key::RightAlt, VK_MENU, EXTENDED);
    AssertKey(Neuron::Key::LeftSystem, VK_LWIN);
    AssertKey(Neuron::Key::RightSystem, VK_RWIN);
  }

  TEST_METHOD(NamesPunctuationByPosition)
  {
    AssertKey(Neuron::Key::Grave, VK_OEM_3);
    AssertKey(Neuron::Key::Minus, VK_OEM_MINUS);
    AssertKey(Neuron::Key::Equal, VK_OEM_PLUS);
    AssertKey(Neuron::Key::LeftBracket, VK_OEM_4);
    AssertKey(Neuron::Key::RightBracket, VK_OEM_6);
    AssertKey(Neuron::Key::Backslash, VK_OEM_5);
    AssertKey(Neuron::Key::Semicolon, VK_OEM_1);
    AssertKey(Neuron::Key::Apostrophe, VK_OEM_7);
    AssertKey(Neuron::Key::Comma, VK_OEM_COMMA);
    AssertKey(Neuron::Key::Period, VK_OEM_PERIOD);
    AssertKey(Neuron::Key::Slash, VK_OEM_2);
    AssertKey(Neuron::Key::Enter, VK_RETURN);
    AssertKey(Neuron::Key::Enter, VK_RETURN, EXTENDED);
  }

  TEST_METHOD(LeavesOtherKeysUnknown)
  {
    AssertKey(Neuron::Key::Unknown, VK_CAPITAL);
    AssertKey(Neuron::Key::Unknown, VK_NUMLOCK);
    AssertKey(Neuron::Key::Unknown, VK_SNAPSHOT);
    AssertKey(Neuron::Key::Unknown, VK_DECIMAL);
    AssertKey(Neuron::Key::Unknown, VK_F16);
    AssertKey(Neuron::Key::Unknown, 0);
  }

  TEST_METHOD(ReachesEveryKey)
  {
    const std::array<std::uint32_t, 4> keyData = {0, EXTENDED, ScanCodeOf(VK_LSHIFT), ScanCodeOf(VK_RSHIFT)};
    std::array<bool, KEY_COUNT + 1> reached{};
    for (std::uint32_t virtualKey = 0; virtualKey < 256; ++virtualKey)
    {
      for (const std::uint32_t data : keyData)
      {
        reached[static_cast<std::size_t>(Neuron::KeyFromVirtualKey(virtualKey, data))] = true;
      }
    }
    Assert::AreEqual(KEY_COUNT, static_cast<int>(Neuron::Key::Pause), L"Pause is the last key");
    for (int key = 1; key <= KEY_COUNT; ++key)
    {
      Assert::IsTrue(reached[static_cast<std::size_t>(key)], (L"no virtual key reaches key " + std::to_wstring(key)).c_str());
    }
  }
};

} // namespace NeuronClientTests
