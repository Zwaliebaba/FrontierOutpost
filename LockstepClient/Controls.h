#pragma once

// Controls.h -- one control vocabulary: four states and a lock, the ink table, and the button (ADR-111).
//
// The same five states are applied to a button, a tile, a sheet row and a rail row, and the hue never
// changes between them -- only the fill, the border's style and the alpha -- so a player learns one
// vocabulary rather than one per surface. `ControlInkFor` is the one table every control on the main
// page reads; the page's own units compose the controls and this header says what they are drawn as.

#include "Color.h"
#include "FontRenderer.h"
#include "ShapeRenderer.h"

#include <cstdint>
#include <string>

namespace Lockstep
{

/// What a control IS, which is the whole of how it is drawn.
///
/// **Four states and a lock, and the same five are applied to a button, a tile, a sheet row and a
/// rail row** (ADR-111). The hue never changes between them -- only the fill, the border's style
/// and the alpha -- so a player learns one vocabulary rather than one per surface, and a queued
/// order looks the same wherever they meet it.
enum class ControlState : std::uint8_t
{
  /// The one recommended thing to do. **One per screen** (ADR-089).
  Primary,
  /// Any other order, and every link.
  Outlined,
  /// Yours, queued, and the next tap takes it back (ADR-053).
  Committed,
  /// Cannot be ordered, and the number segment says why. **Never a target**, which is what the
  /// dashed border means and the only thing it means.
  Inert,
  /// At the lock, or with the link down (ADR-065, ADR-085). Not a target, and the one state that is
  /// about the screen rather than about the control.
  Locked
};

/// Which surface a control is. It decides the geometry -- and, in exactly one case, an ink.
enum class ControlKind : std::uint8_t
{
  Button,
  Tile,
  SheetRow,
  RailRow
};

/// The chrome and the inks one state is drawn in, chosen once.
struct ControlInk
{
  /// Fully transparent for a state with no border.
  Neuron::Color border;
  /// Dashed, which only `Inert` is (ADR-111).
  bool dashed = false;
  /// Fully transparent for a state with no fill.
  Neuron::Color fill;
  Neuron::Color label;
  /// The number segment: its ink, the hairline that separates it from the label, and the ground it
  /// sits on. A filled control shades the ground and draws no line; every other state draws the
  /// line and leaves the ground alone, because a shade over a transparent box is a grey box.
  Neuron::Color number;
  Neuron::Color segmentRule;
  Neuron::Color segmentFill;
};

/// The one state table, read by every control on this page.
///
/// `_moneyReason` is what makes an inert control's number amber: money is the reason a player can
/// do something about before the lock, and the board is not.
[[nodiscard]] ControlInk ControlInkFor(ControlState _state, ControlKind _kind, bool _hovered = false, bool _moneyReason = false) noexcept;

/// One control's box: its fill, then its border.
///
/// **Four dashed edges rather than a dashed rectangle**, because `ShapeRenderer` dashes a line and
/// has no dashed box, and a fifth primitive for one border style is a primitive to keep in step
/// with `StrokeRect` forever. Each edge is drawn half a pixel inside the bounds for the reason
/// `StrokeRect` insets its own: a 1px border lies INSIDE the box it was given, so it never bleeds
/// into the pixel the thing beside it owns.
void DrawControlBox(Neuron::ShapeRenderer& _shapes, float _xPixels, float _yPixels, float _widthPixels, float _heightPixels,
                    const ControlInk& _ink);

/// One button, composed before it is measured and drawn.
///
/// **A number never lives inside the label** (ADR-111). `BUILD 20 CR` was one string, so a queued
/// one became `BUILD 20 CR - QUEUED` and a dear one `BUILD 45 CR - NEED 19 MORE`: a label that
/// grows a suffix every time the state changes, in a column that drops a button that does not fit.
/// The number is its own cell, and the state is said by the chrome rather than spelled out.
struct Button
{
  /// Shouted, mono Medium.
  std::string label;
  /// The second cell: `20 CR`, `-20`, `10 SHIPS`, `NEED 19 MORE`, `AFTER T14`. Empty draws one cell.
  std::string number;
  ControlState state = ControlState::Outlined;
  /// Whether an inert button's reason is money, which is the one an amber number is for.
  bool moneyReason = false;
};

/// How wide a button comes out, in the face it is drawn in.
///
/// Measured rather than assumed, and measured with the segment INCLUDED, because the digest drops a
/// button that does not fit its column and a measurement of the label alone would drop the wrong
/// ones (ADR-053).
[[nodiscard]] float ButtonWidth(const Button& _button);

/// A button's two cells, drawn at a width the caller has already decided it can afford.
void DrawButton(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, float _xPixels, float _yPixels, float _widthPixels,
                const Button& _button, const ControlInk& _ink);

} // namespace Lockstep
