#pragma once

namespace Neuron
{

/// Turns a tap or a click into a point on the 640x400 virtual screen.
///
/// One code path for touch and mouse, through the Windows Pointer API. Touch is the primary input
/// and the mouse is the fallback (MVP-01 section 2), and EnableMouseInPointer is what makes that
/// one path rather than two: with it on, a mouse click arrives as WM_POINTERDOWN exactly as a
/// finger does. There is no WM_LBUTTONDOWN handler here, and there should not be one unless
/// EnableMouseInPointer is ever found to be unavailable.
///
/// There is no keyboard handling either, and that is a decision rather than an omission: this
/// game has no keyboard control at all.
class PointerInput
{
public:
  /// Asks Windows to deliver mouse input as pointer messages. Call once, before the window is
  /// created. False means the process will get no pointer messages from a mouse, which is a thing
  /// to report rather than to work around.
  [[nodiscard]] static bool EnableMouseAsPointer() noexcept;

  /// Not noexcept: a present scale of zero is a broken invariant, and a broken invariant throws
  /// in this tree (Debug.h).
  void Create(HWND _window, std::uint32_t _presentScale);

  /// Feeds a window message. True when it was consumed.
  bool HandleMessage(UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept;

  /// Takes the pending click, in virtual texels from the top-left of the 640x400 screen, and
  /// clears it.
  ///
  /// Only the most recent click is kept. A second tap before the frame that reads the first
  /// replaces it, which is what a player means: the ship goes where they last pointed.
  [[nodiscard]] bool TakeClick(float& _outXTexels, float& _outYTexels) noexcept;

private:
  HWND m_window = nullptr;
  /// The whole-number factor the virtual screen is blown up by. Dividing by it is the entire
  /// conversion from a physical client pixel to a virtual one, and it is exact because the factor
  /// is a whole number (Design/README.md section 1).
  std::uint32_t m_presentScale = 1;

  bool m_hasClick = false;
  float m_clickXTexels = 0.0F;
  float m_clickYTexels = 0.0F;
};

} // namespace Neuron
