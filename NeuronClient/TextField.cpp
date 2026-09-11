// TextField.cpp -- one line of typed text.

#include "pch.h"
#include "TextField.h"

#include <algorithm>

namespace Neuron
{

void TextField::Set(std::string_view _text)
{
  m_text.clear();
  for (const char letter : _text)
  {
    if (m_text.size() >= m_limit)
    {
      break;
    }
    if (letter >= FIRST_PRINTABLE && letter <= LAST_PRINTABLE)
    {
      m_text.push_back(letter);
    }
  }
  m_caret = m_text.size();
}

void TextField::Type(char _character)
{
  // The limit is a refusal, not a truncation. A field that silently dropped the last character
  // typed would look like a dropped keystroke, and a player would try again.
  if (m_text.size() >= m_limit || _character < FIRST_PRINTABLE || _character > LAST_PRINTABLE)
  {
    return;
  }

  m_text.insert(m_caret, 1, _character);
  ++m_caret;
}

void TextField::Backspace()
{
  if (m_caret == 0)
  {
    return;
  }
  --m_caret;
  m_text.erase(m_caret, 1);
}

void TextField::Delete()
{
  if (m_caret >= m_text.size())
  {
    return;
  }
  m_text.erase(m_caret, 1);
}

void TextField::CaretLeft()
{
  m_caret = m_caret == 0 ? 0 : m_caret - 1;
}

void TextField::CaretRight()
{
  m_caret = std::min(m_caret + 1, m_text.size());
}

void TextField::CaretHome()
{
  m_caret = 0;
}

void TextField::CaretEnd()
{
  m_caret = m_text.size();
}

void TextField::Clear()
{
  m_text.clear();
  m_caret = 0;
}

std::string TextField::Shown(bool _masked) const
{
  if (!_masked)
  {
    return m_text;
  }

  // One dot per character, so the field's width and the caret's column are the same whether it is
  // masked or not -- which is what lets SHOW toggle without the text moving.
  return std::string(m_text.size(), '*');
}

} // namespace Neuron
