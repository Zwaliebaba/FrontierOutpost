#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Neuron
{

/// One line of typed text, and the smallest thing that can honestly be called a field.
///
/// **ADR-014's interface layer had no text input of any kind**, and ADR-034 amended it for exactly
/// one reason: the product is a mobile client (blueprint §7), and a phone has no command line, so a
/// client that can only be joined by `--join host:port --token x` is a client that cannot ship.
///
/// What it has: printable ASCII, a caret, backspace and delete, home and end, a length limit, and a
/// masked mode for the token. What it does NOT have is everything else a field normally has —
/// selection, clipboard, undo, multi-line, IME, word motion, scrolling. Each of those is absent
/// because nothing has needed it, and each would be a decision of its own. A field that quietly
/// grew them all would be a widget toolkit, which is the thing ADR-014 exists to not have.
///
/// **It holds no state about being focused.** Whether this field or another one receives a
/// keystroke is the screen's business, and a field that decided that for itself would make two
/// fields on one screen an argument rather than a layout.
class TextField
{
public:
  /// ASCII only, and deliberately. The font is 96 glyphs (ADR-014) — there is nothing to draw a
  /// character outside this range with, so accepting one would store text the screen cannot show.
  static constexpr char FIRST_PRINTABLE = ' ';
  static constexpr char LAST_PRINTABLE = '~';

  explicit TextField(std::size_t _limit = 64)
    : m_limit(_limit)
  {
  }

  /// Replaces the text and puts the caret at the end, which is where somebody who has just been
  /// handed a remembered value wants it.
  void Set(std::string_view _text);

  [[nodiscard]] const std::string& Text() const noexcept
  {
    return m_text;
  }
  [[nodiscard]] std::size_t Caret() const noexcept
  {
    return m_caret;
  }
  [[nodiscard]] bool Empty() const noexcept
  {
    return m_text.empty();
  }

  /// A printable character, at the caret. Anything else is ignored, including the control codes a
  /// `WM_CHAR` delivers for Return, Escape and Backspace — those arrive as keys, below.
  void Type(char _character);

  void Backspace();
  void Delete();
  void CaretLeft();
  void CaretRight();
  void CaretHome();
  void CaretEnd();
  void Clear();

  /// What to draw: the text, or a row of dots when masked and not revealed.
  ///
  /// **The mask is a display rule and not a storage one.** The field always holds the real
  /// characters — a token that could not be read back would be a token nobody could check they had
  /// typed correctly, and ADR-029 is clear that this is a seat and not a secret.
  [[nodiscard]] std::string Shown(bool _masked) const;

  /// Where the caret sits, in characters from the left. The same in masked and plain text, because
  /// a dot is one glyph exactly as its character was.
  [[nodiscard]] std::size_t CaretColumn() const noexcept
  {
    return m_caret;
  }

private:
  std::string m_text;
  std::size_t m_caret = 0;
  std::size_t m_limit;
};

} // namespace Neuron
