// PointerInput.cpp -- WM_POINTER* to a point on the virtual screen and a number of zoom steps.

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

bool PointerInput::ScreenToVirtual(LPARAM _lParam, float& _outXTexels, float& _outYTexels) const noexcept
{
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
  _outXTexels = static_cast<float>(point.x) / static_cast<float>(m_presentScale);
  _outYTexels = static_cast<float>(point.y) / static_cast<float>(m_presentScale);
  return true;
}

bool PointerInput::HandleMessage(UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept
{
  const auto pointerId = static_cast<std::uint32_t>(GET_POINTERID_WPARAM(_wParam));

  switch (_message)
  {
  // WM_POINTERWHEEL and not WM_MOUSEWHEEL. Measured rather than assumed: with
  // EnableMouseInPointer on, a real wheel notch on real hardware arrives here as 0x024E
  // (ADR-009). A WM_MOUSEWHEEL case was carried for a while against the possibility that it did
  // not, and came out once the measurement existed -- it is the same second input path MVP-01
  // section 2 rules out.
  case WM_POINTERWHEEL:
  {
    // The wheel reports in multiples of WHEEL_DELTA, but not necessarily whole ones. Bank the
    // remainder so that a high-resolution wheel adds up to notches instead of never reaching one.
    m_wheelRemainder += GET_WHEEL_DELTA_WPARAM(_wParam);
    m_zoomSteps += m_wheelRemainder / WHEEL_DELTA;
    m_wheelRemainder %= WHEEL_DELTA;
    return true;
  }

  case WM_POINTERDOWN:
  {
    float xTexels = 0.0F;
    float yTexels = 0.0F;
    if (!ScreenToVirtual(_lParam, xTexels, yTexels))
    {
      return false;
    }

    AddContact(pointerId, xTexels, yTexels);

    if (m_contactCount >= MAX_CONTACTS)
    {
      // The second finger turns what looked like a tap into a pinch. Forget the tap: the player
      // is zooming, not ordering the ship to wherever their first finger landed.
      m_hasClick = false;
      m_pinchBaseline = ContactSeparation();
    }
    else
    {
      m_clickXTexels = xTexels;
      m_clickYTexels = yTexels;
      m_hasClick = true;
    }

    return true;
  }

  case WM_POINTERUPDATE:
  {
    float xTexels = 0.0F;
    float yTexels = 0.0F;
    if (!ScreenToVirtual(_lParam, xTexels, yTexels))
    {
      return false;
    }

    MoveContact(pointerId, xTexels, yTexels);
    EvaluatePinch();
    return true;
  }

  case WM_POINTERUP:
  case WM_POINTERCAPTURECHANGED:
  {
    RemoveContact(pointerId);
    return true;
  }

  default:
    return false;
  }
}

void PointerInput::AddContact(std::uint32_t _pointerId, float _xTexels, float _yTexels) noexcept
{
  // A pointer that is already tracked is a repeat rather than a new contact; move it instead.
  for (std::size_t index = 0; index < m_contactCount; ++index)
  {
    if (m_contacts[index].pointerId == _pointerId)
    {
      m_contacts[index] = {_pointerId, _xTexels, _yTexels};
      return;
    }
  }

  // A third finger is ignored rather than replacing one of the two that make the pinch, so that
  // resting a hand on the glass does not make the zoom jump.
  if (m_contactCount < MAX_CONTACTS)
  {
    m_contacts[m_contactCount] = {_pointerId, _xTexels, _yTexels};
    ++m_contactCount;
  }
}

void PointerInput::MoveContact(std::uint32_t _pointerId, float _xTexels, float _yTexels) noexcept
{
  for (std::size_t index = 0; index < m_contactCount; ++index)
  {
    if (m_contacts[index].pointerId == _pointerId)
    {
      m_contacts[index].xTexels = _xTexels;
      m_contacts[index].yTexels = _yTexels;
      return;
    }
  }
}

void PointerInput::RemoveContact(std::uint32_t _pointerId) noexcept
{
  for (std::size_t index = 0; index < m_contactCount; ++index)
  {
    if (m_contacts[index].pointerId != _pointerId)
    {
      continue;
    }

    m_contacts[index] = m_contacts[m_contactCount - 1];
    --m_contactCount;

    // Lifting either finger ends the pinch. Putting it back down starts a new one from wherever
    // the fingers now are, rather than resuming against a stale baseline.
    m_pinchBaseline = 0.0F;
    return;
  }
}

float PointerInput::ContactSeparation() const noexcept
{
  if (m_contactCount < MAX_CONTACTS)
  {
    return 0.0F;
  }

  const float deltaX = m_contacts[1].xTexels - m_contacts[0].xTexels;
  const float deltaY = m_contacts[1].yTexels - m_contacts[0].yTexels;
  return std::sqrt(deltaX * deltaX + deltaY * deltaY);
}

void PointerInput::EvaluatePinch() noexcept
{
  if (m_contactCount < MAX_CONTACTS || m_pinchBaseline <= 0.0F)
  {
    return;
  }

  const float separation = ContactSeparation();
  if (separation <= 0.0F)
  {
    return;
  }

  // A loop rather than a single compare, because one WM_POINTERUPDATE can carry a large jump --
  // a fast pinch, or a frame the application missed -- and banking one step for it would make the
  // zoom lag the fingers.
  while (separation >= m_pinchBaseline * PINCH_STEP_RATIO)
  {
    ++m_zoomSteps;
    m_pinchBaseline *= PINCH_STEP_RATIO;
  }
  while (separation <= m_pinchBaseline / PINCH_STEP_RATIO)
  {
    --m_zoomSteps;
    m_pinchBaseline /= PINCH_STEP_RATIO;
  }
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

std::int32_t PointerInput::TakeZoomSteps() noexcept
{
  const std::int32_t steps = m_zoomSteps;
  m_zoomSteps = 0;
  return steps;
}

} // namespace Neuron
