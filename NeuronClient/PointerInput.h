#pragma once

namespace Neuron
{

/// Turns taps, drags and wheel notches into the two things this game's input means: a point on the
/// 1280x720 screen to go to, and a number of zoom steps.
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

  /// The window whose client area the screen is. There is no scale factor to pass: the client
  /// area is exactly the 1280x720 the game renders, so a client pixel IS a screen pixel and the
  /// conversion is ScreenToClient and nothing else (ADR-011).
  void Create(HWND _window) noexcept;

  /// Feeds a window message. True when it was consumed.
  bool HandleMessage(UINT _message, WPARAM _wParam, LPARAM _lParam) noexcept;

  /// How far a contact may move and still count as a tap rather than a drag.
  ///
  /// A finger never lands and lifts on exactly one pixel, so without a slop every tap on a
  /// touchscreen would be a one-pixel drag and nothing would ever be tapped. Four pixels is
  /// comfortably inside the smallest thing on the screen that can be tapped (an 18px button) and
  /// comfortably outside the jitter of a finger.
  static constexpr float TAP_SLOP_PIXELS = 4.0F;

  /// A drag in progress: how far since the last frame, and where the finger first went down.
  ///
  /// The ORIGIN is what makes this useful to a screen with more than one pane. A drag belongs to
  /// whatever was under the press, not to whatever the finger happens to be over now -- so a
  /// rotation that starts on the map keeps rotating the map even when the finger crosses onto a
  /// rail.
  struct Drag
  {
    float deltaXPixels;
    float deltaYPixels;
    float originXPixels;
    float originYPixels;
  };

  /// Takes the movement accumulated since the last call, and clears it. False when the player is
  /// not dragging.
  [[nodiscard]] bool TakeDrag(Drag& _outDrag) noexcept;

  /// Takes the pending tap, in pixels from the top-left of the 1280x720 screen, and clears it.
  ///
  /// A TAP IS DOWN AND UP, not down. It has to be: a press that turns into a drag must not also
  /// have opened whatever was under it, and until the finger lifts (or moves past TAP_SLOP_PIXELS)
  /// there is no way to know which it was. Before the map could be rotated this fired on
  /// WM_POINTERDOWN, because nothing on the screen could be dragged (ADR-016).
  ///
  /// Only the most recent tap is kept. A second tap before the frame that reads the first
  /// replaces it, which is what a player means: the ship goes where they last pointed.
  ///
  /// A tap that turned out to be the start of a pinch is NOT reported. The first finger of a
  /// pinch looks exactly like a tap until the second one lands, so the tap is cancelled when it
  /// does -- otherwise every pinch would also fling the ship at wherever the first finger
  /// happened to touch down.
  [[nodiscard]] bool TakeClick(float& _outXPixels, float& _outYPixels) noexcept;

  /// Takes the zoom the player has asked for since the last frame, in whole steps, and clears it.
  /// Positive zooms in. Zero when they have not asked for any, which is almost every frame.
  [[nodiscard]] std::int32_t TakeZoomSteps() noexcept;

private:
  struct Contact
  {
    std::uint32_t pointerId;
    float xPixels;
    float yPixels;
  };

  /// Desktop coordinates out of a WM_POINTER* lParam, converted to client-area pixels.
  [[nodiscard]] bool ScreenToClientPixels(LPARAM _lParam, float& _outXPixels, float& _outYPixels) const noexcept;

  void AddContact(std::uint32_t _pointerId, float _xPixels, float _yPixels) noexcept;
  void MoveContact(std::uint32_t _pointerId, float _xPixels, float _yPixels) noexcept;
  void RemoveContact(std::uint32_t _pointerId) noexcept;

  /// The distance between the two contacts, or 0 when there are not two.
  [[nodiscard]] float ContactSeparation() const noexcept;

  /// Compares the current separation against the baseline and banks whole steps.
  void EvaluatePinch() noexcept;

  HWND m_window = nullptr;

  /// The press that is currently down, and whether it has moved far enough to stop being a tap.
  /// One finger only: a second contact turns the gesture into a pinch and cancels both.
  bool m_pressActive = false;
  bool m_pressBecameDrag = false;
  float m_pressOriginXPixels = 0.0F;
  float m_pressOriginYPixels = 0.0F;
  float m_pressLastXPixels = 0.0F;
  float m_pressLastYPixels = 0.0F;

  /// Drag movement banked since the last frame. Accumulated rather than sampled, so a frame that
  /// took longer than usual rotates by everything the finger did during it rather than by the
  /// last message only.
  float m_dragDeltaXPixels = 0.0F;
  float m_dragDeltaYPixels = 0.0F;

  bool m_hasClick = false;
  float m_clickXPixels = 0.0F;
  float m_clickYPixels = 0.0F;

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
