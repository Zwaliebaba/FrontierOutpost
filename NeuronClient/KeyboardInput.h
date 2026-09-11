#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Neuron
{

/// The keyboard, which this tree did not have until a field needed one.
///
/// **ADR-014 says there is no keyboard handling at all**, and that was true and fine for a screen
/// whose whole vocabulary is taps: the game has no chat, no free text and no commands to type
/// (one-pager, "What it is not"). ADR-034 amended it for the join screen alone — a server address
/// and a token — and this class exists to keep that amendment small.
///
/// **It reports characters and a handful of named keys, and nothing else.** No modifiers, no key
/// repeat state, no chords, no scan codes. A caller asks what was typed since the last frame and
/// gets a short list; anything it cannot express is a key this game does not use.
///
/// WM_CHAR AND NOT WM_KEYDOWN, for the characters. `WM_CHAR` is the message that has already been
/// through the keyboard layout, so an AZERTY keyboard types what its owner expects and a dead key
/// resolves before it arrives. `WM_KEYDOWN` carries virtual keys, which is the right message for
/// Backspace and the arrows and the wrong one for letters.
class KeyboardInput
{
public:
  /// The keys that are not characters. Everything a single-line field needs and nothing else.
  enum class Key : std::uint8_t
  {
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    Enter,
    Tab,
    Escape
  };

  /// How much one frame may type before the rest is dropped.
  ///
  /// **Fixed buffers, because this is called from a window procedure and must not throw.** A
  /// `std::string::push_back` can allocate, an allocation can throw, and an exception crossing a
  /// Win32 callback is not something the standard has an opinion about -- clang-tidy's
  /// `bugprone-exception-escape` is right to refuse it. Bounding the input removes the allocation
  /// rather than the `noexcept`.
  ///
  /// Thirty-two characters is far more than a frame of human typing; anything past it is a stuck
  /// key or a paste, and this game has neither.
  static constexpr std::size_t MAXIMUM_TYPED = 32;
  static constexpr std::size_t MAXIMUM_KEYS = 16;

  /// Feeds a window message. Returns true when it was consumed.
  [[nodiscard]] bool HandleMessage(std::uint32_t _message, std::uint64_t _wParam, std::int64_t _lParam) noexcept;

  /// The printable characters typed since the last take, in order. Taken and cleared: a frame that
  /// does not ask has not had them, and a frame that asks twice does not get them twice.
  [[nodiscard]] std::string TakeTyped();

  /// The named keys pressed since the last take, in order. Taken and cleared, for the same reason.
  [[nodiscard]] std::vector<Key> TakeKeys();

private:
  /// Records a named key, or drops it when the frame's buffer is full. Never throws.
  void Remember(Key _key) noexcept;

  std::array<char, MAXIMUM_TYPED> m_typed = {};
  std::size_t m_typedCount = 0;

  std::array<Key, MAXIMUM_KEYS> m_keys = {};
  std::size_t m_keyCount = 0;
};

} // namespace Neuron
