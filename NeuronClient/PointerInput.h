#pragma once

namespace Neuron
{

/// Turns taps, drags and wheel notches into the two things this game's input means: a point on the
/// 640x400 virtual screen to go to, and a number of zoom steps.
///
/// One code path for touch and mouse, through the Windows Pointer API. Touch is the primary input
/// and the mouse is the fallback (MVP-01 section 2), and EnableMouseInPointer is what makes that
/// one path rather than two: with it on, a mouse click arrives as WM_POINTERDOWN exactly as a
/// finger does, and a wheel notch arrives as WM_POINTERWHEEL. There is no WM_LBUTTONDOWN handler
/// here and no keyboard handling at all.
///
/// ZOOM HAS TWO PRODUCERS AND ONE INTENT. Windows has no single "zoom" event: the wheel is one
/// message carrying a signed delta, and a pinch is two contacts whose separation you have to
/// track yourself across three messages. Both converge here on TakeZoomSteps(), so the camera
/// never learns which one the player used (ADR-008).
class PointerInput
{
public:
  /// A pinch changes the zoom by one step each time the distance between the two contacts changes
  /// by this factor, and then re-baselines. Chosen so a comfortable pinch across a phone-sized
  /// screen is two or three steps rather than the whole range: too small and the zoom races away
  /// under your fingers, too large and the gesture feels dead.
  static constexpr float PINCH_STEP_RATIO = 1.25F;

  /// Two, and there is no reason for it to be more. A pinch is defined by two contacts; a third
  /// finger on the glass is not a bigger pinch, it is a different gesture this game does not have.
  static constexpr std::size_t MAX_CONTACTS = 2;

  /// Asks Windows to deliver mouse input as pointer messages. Call once, before the window is
  /// created. False means the process will get no pointer messages from a mouse, which is a thing
  /// to report rather than to work around.
  [[nodiscard]] static bool EnableMouseAsPointer() noexcept;

  /// Not noexcept: a present scale of zero is a broken invariant, and a broken invariant throws
  /// in this tree (Debug.h).
  void Create(HWND _window, std::uint32_t _presentScale);

  /// Feeds a window message. True when it was consumed.
  bool HandleMessage(UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept;

  /// Takes the pending tap, in virtual texels from the top-left of the 640x400 screen, and clears
  /// it.
  ///
  /// Only the most recent tap is kept. A second tap before the frame that reads the first
  /// replaces it, which is what a player means: the ship goes where they last pointed.
  ///
  /// A tap that turned out to be the start of a pinch is NOT reported. The first finger of a
  /// pinch looks exactly like a tap until the second one lands, so the tap is cancelled when it
  /// does -- otherwise every pinch would also fling the ship at wherever the first finger
  /// happened to touch down.
  [[nodiscard]] bool TakeClick(float& _outXTexels, float& _outYTexels) noexcept;

  /// Takes the zoom the player has asked for since the last frame, in whole steps, and clears it.
  /// Positive zooms in. Zero when they have not asked for any, which is almost every frame.
  [[nodiscard]] std::int32_t TakeZoomSteps() noexcept;

private:
  struct Contact
  {
    std::uint32_t pointerId;
    float xTexels;
    float yTexels;
  };

  /// Screen coordinates out of a WM_POINTER* lParam, converted to virtual texels.
  [[nodiscard]] bool ScreenToVirtual(LPARAM _lParam, float& _outXTexels, float& _outYTexels) const noexcept;

  void AddContact(std::uint32_t _pointerId, float _xTexels, float _yTexels) noexcept;
  void MoveContact(std::uint32_t _pointerId, float _xTexels, float _yTexels) noexcept;
  void RemoveContact(std::uint32_t _pointerId) noexcept;

  /// The distance between the two contacts, or 0 when there are not two.
  [[nodiscard]] float ContactSeparation() const noexcept;

  /// Compares the current separation against the baseline and banks whole steps.
  void EvaluatePinch() noexcept;

  HWND m_window = nullptr;
  /// The whole-number factor the virtual screen is blown up by. Dividing by it is the entire
  /// conversion from a physical client pixel to a virtual one, and it is exact because the factor
  /// is a whole number (Design/README.md section 1).
  std::uint32_t m_presentScale = 1;

  bool m_hasClick = false;
  float m_clickXTexels = 0.0F;
  float m_clickYTexels = 0.0F;

  std::array<Contact, MAX_CONTACTS> m_contacts = {};
  std::size_t m_contactCount = 0;
  /// The separation the current pinch last banked a step at.
  float m_pinchBaseline = 0.0F;

  std::int32_t m_zoomSteps = 0;
  /// Wheel delta below a whole notch, kept rather than thrown away. A high-resolution wheel sends
  /// deltas smaller than WHEEL_DELTA, and discarding the remainder makes such a wheel do nothing
  /// at all no matter how far it is turned.
  std::int32_t m_wheelRemainder = 0;
};

} // namespace Neuron
