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
/// The connection dialog's card, which is opaque where every other card is a wash: it sits over a
/// scrim and over the board, and a translucent card would let the map show through the one surface
/// that has to be read before anything else. It is the second ground text is measured against
/// (`ContrastTests`).
inline constexpr Neuron::Color DIALOG_FILL = {17, 21, 29, 255};
inline constexpr Neuron::Color CARD_BORDER = {255, 255, 255, 26};
inline constexpr Neuron::Color DIVIDER = {255, 255, 255, 18};
inline constexpr Neuron::Color OUTLINE = {255, 255, 255, 51};
inline constexpr Neuron::Color HOVER_FILL = {255, 255, 255, 20};

/// **The alphas are a measured floor, not a taste** (ADR-083). Every one of these is an alpha over a
/// dark ground, which is exactly where a palette drifts below legible without anyone noticing: the
/// ink still looks like the ink, it is just fainter. `ContrastTests` holds all four to WCAG AA's
/// 4.5:1 over both grounds this game paints text on, and `NEUTRAL_DIM` at 115 measured 3.61:1.
///
/// `TEXT_DETAIL` and `TEXT_MUTED` are the same byte and keep two names: they mean different things
/// -- a sentence under a title, and a label beside one -- and the day either moves it will move
/// alone.
inline constexpr Neuron::Color TEXT_PRIMARY = {240, 243, 247, 255};
inline constexpr Neuron::Color TEXT_MUTED = {214, 220, 228, 168};
inline constexpr Neuron::Color TEXT_DETAIL = {214, 220, 228, 168};
inline constexpr Neuron::Color NEUTRAL_DIM = {214, 220, 228, 140};

inline constexpr Neuron::Color BLUE = {94, 196, 255, 255};
inline constexpr Neuron::Color AMBER = {255, 196, 87, 255};
inline constexpr Neuron::Color RED = {255, 110, 96, 255};
inline constexpr Neuron::Color PURPLE = {170, 140, 255, 255};

/// The filled grey a locked rail wears (SCREENS.md 06). Solid rather than an outline, because at
/// the lock the rail stops being a list of things you could change and becomes a receipt.
inline constexpr Neuron::Color LOCKED_FILL = {214, 220, 228, 150};
/// A star in the sky behind every screen. Not text and not held to the contrast floor: it is meant
/// to be faint, and a legible star is a defect.
inline constexpr Neuron::Color STAR = {214, 220, 228, 220};

/// A station's two tones (ADR-103). The lit tone is the owner's colour; the dark tone is the same
/// colour moved this far toward the app background, made once on the CPU by `Shaded`. There is no
/// third tone: the shader picks one of the two per pixel and never mixes them (ADR-012).
///
/// 0.58 rather than a halving because the owner colours are already bright on a near-black
/// ground, and a dark side that stayed vivid read as a second owner rather than as the same ball
/// turned away from the light.
inline constexpr float STATION_SHADE = 0.58F;

/// The band between the lit one and the shadow: the same colour a shorter way toward the
/// background (ADR-105). 0.30 against 0.58 puts it halfway down the ramp in blue -- 255, 185, 119
/// -- so two adjacent bands two pixels wide are still told apart, which is the whole difficulty at
/// this ball size.
inline constexpr float STATION_HALF_SHADE = 0.30F;

/// How far the silhouette's tone is lifted toward white (ADR-104). The rim is a THIRD authored
/// tone, not a computed one -- the shader selects it exactly as it selects the other two -- and
/// white rather than the owner's own colour because it is the light getting past the ball rather
/// than the ball's own material. It keeps its distance from every owner colour, so a rimmed rival
/// is still that rival.
inline constexpr float STATION_RIM = 0.45F;

/// How far the glint is lifted toward white -- further than the rim, because it is the brightest
/// thing on a station and the one that says the surface is hard rather than matte (ADR-106).
inline constexpr float STATION_GLINT = 0.75F;

/// The station's inks, as alphas over the owner's colour: its contact shadow (black), the disc it
/// stands on, the dashed footprint whose reach is its yield, the stem whose height is its yield,
/// and the yield written under its foot.
inline constexpr std::uint8_t STATION_SHADOW_ALPHA = 115;
inline constexpr std::uint8_t STATION_DISC_ALPHA = 70;
inline constexpr std::uint8_t STATION_FOOTPRINT_ALPHA = 115;
inline constexpr std::uint8_t STATION_STEM_ALPHA = 180;
inline constexpr std::uint8_t STATION_YIELD_ALPHA = 190;

/// The dark tone for a lit one. Opaque, because the mesh pass does not blend and a ball with a
/// translucent dark side would be a ball with a hole in it.
[[nodiscard]] constexpr Neuron::Color Shaded(const Neuron::Color& _lit) noexcept
{
  return Neuron::Mix(_lit, APP_BACKGROUND, STATION_SHADE);
}

/// The band the light only grazes.
[[nodiscard]] constexpr Neuron::Color HalfLit(const Neuron::Color& _lit) noexcept
{
  return Neuron::Mix(_lit, APP_BACKGROUND, STATION_HALF_SHADE);
}

/// The silhouette's tone for a lit one. Opaque, for the reason `Shaded` is.
[[nodiscard]] constexpr Neuron::Color Rimmed(const Neuron::Color& _lit) noexcept
{
  return Neuron::Mix(_lit, Neuron::WHITE, STATION_RIM);
}

/// The highlight's tone for a lit one.
[[nodiscard]] constexpr Neuron::Color Glinted(const Neuron::Color& _lit) noexcept
{
  return Neuron::Mix(_lit, Neuron::WHITE, STATION_GLINT);
}

/// A surface's whole ramp from one lit colour: the four tones the mesh pass chooses between
/// (ADR-105). **Derived rather than authored four times over**, so an owner colour is still the one
/// thing a caller states -- which is what keeps twelve owners from becoming forty-eight literals.
[[nodiscard]] constexpr Neuron::ColorRamp RampFor(const Neuron::Color& _lit) noexcept
{
  return Neuron::ColorRamp{Shaded(_lit), HalfLit(_lit), _lit, Rimmed(_lit), Glinted(_lit)};
}

/// The same ramp for a FLAT-FACED solid, with the silhouette tone turned off by setting it to the
/// shadow -- which is what ADR-104's strict-superset property is for.
///
/// The glint is switched off the same way, by passing the lit tone: a flat face's half-vector dot
/// is constant across the whole face, so a face that crossed the threshold would turn white all at
/// once rather than carrying a highlight.
///
/// **A rim is a curved-surface effect and a flat face cannot have one.** The shader reads a low
/// dot(normal, toViewer) as "this is the limb", which is true on a sphere and false on a column: a
/// column's far face is oblique across its whole area, so the whole face tripped the test and came
/// out brighter than the face the light was actually finding. Measured on 2026-09-14 before this
/// existed, as a stem with a white side (ADR-105).
[[nodiscard]] constexpr Neuron::ColorRamp FlatRampFor(const Neuron::Color& _lit) noexcept
{
  return Neuron::ColorRamp{Shaded(_lit), HalfLit(_lit), _lit, Shaded(_lit), _lit};
}

// The ramp runs the right way round at every step, as ADR-012 had every pair asserted: a station is
// never darker where the light finds it than where it grazes it, never darker where it grazes than
// in the shadow, and never dimmer on the limb the light gets past than on the face it finds.
static_assert(Neuron::Luminance(Shaded(BLUE)) < Neuron::Luminance(HalfLit(BLUE)));
static_assert(Neuron::Luminance(HalfLit(BLUE)) < Neuron::Luminance(BLUE));
static_assert(Neuron::Luminance(BLUE) < Neuron::Luminance(Rimmed(BLUE)));
static_assert(Neuron::Luminance(Shaded(AMBER)) < Neuron::Luminance(HalfLit(AMBER)));
static_assert(Neuron::Luminance(HalfLit(AMBER)) < Neuron::Luminance(AMBER));
static_assert(Neuron::Luminance(AMBER) < Neuron::Luminance(Rimmed(AMBER)));
static_assert(Neuron::Luminance(Rimmed(BLUE)) < Neuron::Luminance(Glinted(BLUE)));
static_assert(Neuron::Luminance(Rimmed(AMBER)) < Neuron::Luminance(Glinted(AMBER)));

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

/// **The smallest target a finger hits reliably** (ADR-098, ADR-100), in both dimensions.
///
/// It lives here, with the frame, because it is a rule about every screen and not a constant of
/// one. It was `MainPage::SHEET_ROW_HEIGHT`'s comment for a year, applied to sheet rows and to
/// nothing else, and that is exactly what a number with no home does.
///
/// **How a control reaches it depends on what it sits beside.** A target in a COLUMN OF SIBLINGS
/// grows its box, and the things under it move down. An ISOLATED CHIP grows only its hit: it stays
/// the size the layout around it needs, with a rectangle this big centred on it. A control that is
/// neither and cannot be this big says why where it is declared.
inline constexpr float TOUCH_FLOOR = 44.0F;

/// A rectangle, for the one function below that has to return four numbers.
struct Box
{
  float x;
  float y;
  float width;
  float height;
};

/// One rectangle grown AROUND ITS MIDDLE to the touch floor in whichever dimension is short.
///
/// **For an isolated chip**, where the drawing stays the size the layout around it needs and only
/// the target grows (ADR-100). Around the middle rather than from the corner, so the chip stays
/// where it is drawn -- a rectangle anchored at the top-left would put the target below a badge
/// that sits on a label's line.
///
/// It is here rather than in each page for the reason the palette is: four screens grow chips this
/// way, and four copies of a `std::max` is four chances for one of them to be adjusted alone.
[[nodiscard]] inline constexpr Box GrownToFloor(float _x, float _y, float _width, float _height) noexcept
{
  const float width = _width < TOUCH_FLOOR ? TOUCH_FLOOR : _width;
  const float height = _height < TOUCH_FLOOR ? TOUCH_FLOOR : _height;
  return Box{_x - (width - _width) * 0.5F, _y - (height - _height) * 0.5F, width, height};
}

} // namespace Frame

/// The same colour at a different opacity. Fading is how this screen says *not now* -- a locked
/// control, a system remembered rather than seen -- and it must not also change the hue.
[[nodiscard]] inline constexpr Neuron::Color WithAlpha(const Neuron::Color& _color, std::uint8_t _alpha) noexcept
{
  return Neuron::Color{_color.red, _color.green, _color.blue, _alpha};
}

/// Labels are shouted, and this is where a name authored in mixed case becomes one.
///
/// **"The font has one case" was the reason until 2026-09-13, and it has stopped being true.** The
/// 8x8 font had no lowercase at all; Plex has both (ADR-074). So the shouting is now a choice the
/// design sheet is making rather than a constraint the font imposes -- and ADR-074 deliberately
/// left open whether it should go on being made. **Do not answer that here.** It is a question
/// about how the screen reads, not about what is baked.
///
/// One thing does depend on the current answer: `FaceRuleTests` tells a label from a sentence by
/// whether it has a lowercase letter in it, which works because every data literal on these screens
/// goes through here or is typed in capitals. If the labels ever stop shouting, that test needs a
/// different discriminator, and the test says so too.
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
