// PointerInput.cpp -- WM_POINTERDOWN to a point on the virtual screen.

#include "pch.h"
#include "PointerInput.h"

namespace Neuron
{

bool PointerInput::EnableMouseAsPointer() noexcept
{
  return EnableMouseInPointer(TRUE) != FALSE;
}

void PointerInput::Create(HWND _window, std::uint32_t _presentScale)
{
  ASSERT_TEXT(_presentScale > 0, L"A present scale of zero would divide by zero.");
  m_window = _window;
  m_presentScale = _presentScale;
}

bool PointerInput::HandleMessage(UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept
{
  UNREFERENCED_PARAMETER(_wParam);

  if (_message != WM_POINTERDOWN)
  {
    return false;
  }

  // WM_POINTER* carries SCREEN coordinates, unlike the mouse messages, and they are signed --
  // a second monitor to the left of the primary one has negative x. Extracting them as unsigned
  // would put a click there at 65000-odd pixels to the right.
  POINT point = {static_cast<LONG>(static_cast<std::int16_t>(LOWORD(_lParam))),
                 static_cast<LONG>(static_cast<std::int16_t>(HIWORD(_lParam)))};
  if (ScreenToClient(m_window, &point) == FALSE)
  {
    return false;
  }

  // Physical client pixels to virtual texels. An exact division, because the scale is a whole
  // number; the fractional part that survives is the position within the virtual pixel, which the
  // camera's un-projection is happy to use.
  m_clickXTexels = static_cast<float>(point.x) / static_cast<float>(m_presentScale);
  m_clickYTexels = static_cast<float>(point.y) / static_cast<float>(m_presentScale);
  m_hasClick = true;

  return true;
}

bool PointerInput::TakeClick(float& _outXTexels, float& _outYTexels) noexcept
{
  if (!m_hasClick)
  {
    return false;
  }

  _outXTexels = m_clickXTexels;
  _outYTexels = m_clickYTexels;
  m_hasClick = false;
  return true;
}

} // namespace Neuron
