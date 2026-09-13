#pragma once

#include "Color.h"
#include "FontRenderer.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Lockstep
{

/// The design tokens, and the three helpers every screen draws with.
///
/// **`Design/Screens/README.md` calls these design tokens and there is one list of them**, but
/// until the map moved out of `MainPage.cpp` there were five: every page carried its own copy of
/// the same dozen colours, because each was written on its own and none of them could see the
/// others. Five copies of a palette is five palettes on the day one of them is adjusted.
///
/// The economy is deliberate and worth keeping: blue is *you*, and also accept, and also a trade
/// lane; amber is a rival, and also warning, and also a countdown; coral is a rival and also loss.
/// A player learns three colours rather than nine (ADR-027).
///
/// The map's own numbers -- grid spacing, stem heights, halo scales -- are not here. They are the
/// map's and they live with it.
namespace Ink
{

inline constexpr Neuron::Color APP_BACKGROUND = {11, 14, 20, 255};

inline constexpr Neuron::Color CARD_FILL = {255, 255, 255, 10};
inline constexpr Neuron::Color CARD_BORDER = {255, 255, 255, 26};
inline constexpr Neuron::Color DIVIDER = {255, 255, 255, 18};
inline constexpr Neuron::Color OUTLINE = {255, 255, 255, 51};
inline constexpr Neuron::Color HOVER_FILL = {255, 255, 255, 20};

inline constexpr Neuron::Color TEXT_PRIMARY = {240, 243, 247, 255};
inline constexpr Neuron::Color TEXT_MUTED = {214, 220, 228, 140};
inline constexpr Neuron::Color TEXT_DETAIL = {214, 220, 228, 153};
inline constexpr Neuron::Color NEUTRAL_DIM = {214, 220, 228, 115};

inline constexpr Neuron::Color BLUE = {94, 196, 255, 255};
inline constexpr Neuron::Color AMBER = {255, 196, 87, 255};
inline constexpr Neuron::Color RED = {255, 110, 96, 255};
inline constexpr Neuron::Color PURPLE = {170, 140, 255, 255};

/// The filled grey a locked rail wears (SCREENS.md 06). Solid rather than an outline, because at
/// the lock the rail stops being a list of things you could change and becomes a receipt.
inline constexpr Neuron::Color LOCKED_FILL = {214, 220, 228, 150};

} // namespace Ink

/// The frame, in the one place that decides it.
///
/// The window is exactly this and cannot be resized (ADR-011), so these are facts rather than
/// defaults: the client area IS the framebuffer, and a rendered pixel is a physical one.
namespace Frame
{

inline constexpr float SCREEN_WIDTH = 1280.0F;
inline constexpr float SCREEN_HEIGHT = 720.0F;

/// The main page's three columns and its bar (SCREENS.md 01). The map is what is left between the
/// two rails, which is why it is not a number of its own.
inline constexpr float TOP_BAR_HEIGHT = 44.0F;
inline constexpr float DIGEST_WIDTH = 400.0F;
inline constexpr float ORDERS_WIDTH = 260.0F;

} // namespace Frame

/// The same colour at a different opacity. Fading is how this screen says *not now* -- a locked
/// control, a system remembered rather than seen -- and it must not also change the hue.
[[nodiscard]] inline constexpr Neuron::Color WithAlpha(const Neuron::Color& _color, std::uint8_t _alpha) noexcept
{
  return Neuron::Color{_color.red, _color.green, _color.blue, _alpha};
}

/// Labels are shouted. The font has one case and the design sheet uses it for every label, so this
/// is where a name authored in mixed case becomes one.
[[nodiscard]] std::string Uppercased(std::string_view _text);

/// A string centred on a pixel column, rather than starting at one.
void DrawCentered(Neuron::FontRenderer& _text, float _centerXPixels, std::int32_t _yPixels, std::string_view _string,
                  const Neuron::Color& _color, Neuron::Face _face = Neuron::FontRenderer::DEFAULT_FACE,
                  std::uint32_t _scale = Neuron::FontRenderer::DEFAULT_SCALE);

/// A string ending at a pixel column. The right-hand half of every rail is laid out from the edge
/// inwards, because its widest member is the one that changes.
void DrawRight(Neuron::FontRenderer& _text, float _rightXPixels, std::int32_t _yPixels, std::string_view _string,
               const Neuron::Color& _color, Neuron::Face _face = Neuron::FontRenderer::DEFAULT_FACE,
               std::uint32_t _scale = Neuron::FontRenderer::DEFAULT_SCALE);

/// The vertical position that centres one line of text in a band.
[[nodiscard]] std::int32_t CenterTextY(float _bandTop, float _bandHeight, Neuron::Face _face = Neuron::FontRenderer::DEFAULT_FACE,
                                       std::uint32_t _scale = Neuron::FontRenderer::DEFAULT_SCALE) noexcept;

/// The top of a band of `_bandHeight` that has to sit around a line of text ALREADY placed.
///
/// The inverse of `CenterTextY`, and it exists because a few places have it the other way round: a
/// chip beside a section header, a button under a card's last detail line. There the text position
/// is the fixed thing -- it is shared with something else on the same row -- and the box has to be
/// put around it.
///
/// **It was three hand-tuned offsets until 2026-09-13**, each of them the number that centred an
/// eight-pixel glyph in its own box, and each of them wrong in the same direction the moment a
/// glyph box became 17px: the chrome crept up into the line above and drew through it. Written as
/// the inverse of the centring it has to agree with, there is nothing left to tune.
[[nodiscard]] float BandTopForText(std::int32_t _textY, float _bandHeight, Neuron::Face _face = Neuron::FontRenderer::DEFAULT_FACE,
                                   std::uint32_t _scale = Neuron::FontRenderer::DEFAULT_SCALE) noexcept;

} // namespace Lockstep
