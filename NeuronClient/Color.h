#pragma once

namespace Neuron
{

/// One color, exactly as the framebuffer stores it: four 8-bit channels, R8G8B8A8 (ADR-011).
///
/// R8: a public aggregate handed to the GPU, so plain fields and brace initialization.
///
/// There is no palette and no index (ADR-011): every pass writes a color directly. What the game
/// keeps is the TABLE OF NAMES below -- it refers to its colors by name rather than by literal.
struct Color
{
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
  std::uint8_t alpha;
};

/// The four bytes in the order DXGI_FORMAT_R8G8B8A8_UNORM reads them: red in the LOW byte.
///
/// This is the one place the channel order is written down, and it is deliberately not the
/// 0x00RRGGBB packing a person would type by hand -- a vertex attribute declared R8G8B8A8_UNORM
/// is read red-first out of memory, and a byte-swapped color is the kind of bug that looks like a
/// deliberate art choice.
[[nodiscard]] inline constexpr std::uint32_t Pack(const Color& _color) noexcept
{
  return static_cast<std::uint32_t>(_color.red) | (static_cast<std::uint32_t>(_color.green) << 8U) |
         (static_cast<std::uint32_t>(_color.blue) << 16U) | (static_cast<std::uint32_t>(_color.alpha) << 24U);
}

/// The sum of the three channels. Not a perceptual measure and not meant to be one: it exists so
/// that "the lit tone is brighter than the shaded tone" is a static_assert rather than an opinion
/// (ADR-012).
[[nodiscard]] inline constexpr std::uint32_t Luminance(const Color& _color) noexcept
{
  return static_cast<std::uint32_t>(_color.red) + static_cast<std::uint32_t>(_color.green) + static_cast<std::uint32_t>(_color.blue);
}

/// `_from` moved `_toward` of the way to `_to`, channel by channel, alpha included: 0 is `_from`,
/// 1 is `_to`, and the byte is rounded rather than truncated.
///
/// **The one place two named colours become a third, on the CPU, at authoring time.** A pass that
/// draws two tones (ADR-012) has to get its second tone from somewhere, and the choice is either
/// a second name per material or a rule applied once, here, to the first. This is the rule. It is
/// deliberately not something a shader has: a colour mixed on the GPU per pixel is the gradient
/// ADR-012 forbids, and a colour mixed here is a value a test can read.
[[nodiscard]] inline constexpr Color Mix(const Color& _from, const Color& _to, float _toward) noexcept
{
  const float toward = _toward < 0.0F ? 0.0F : (_toward > 1.0F ? 1.0F : _toward);
  const auto channel = [toward](std::uint8_t _a, std::uint8_t _b)
  {
    const float mixed = static_cast<float>(_a) + (static_cast<float>(_b) - static_cast<float>(_a)) * toward;
    // Rounded half up, spelled as a floor and a comparison rather than as `+ 0.5` cast to an
    // integer: `mixed` is never negative, so the two agree, and the spelled-out form is the one
    // that stays constexpr and the one clang-tidy accepts.
    const auto whole = static_cast<std::uint32_t>(mixed);
    return static_cast<std::uint8_t>(mixed - static_cast<float>(whole) >= 0.5F ? whole + 1U : whole);
  };
  return Color{channel(_from.red, _to.red), channel(_from.green, _to.green), channel(_from.blue, _to.blue),
               channel(_from.alpha, _to.alpha)};
}

/// Opaque, because nothing in this renderer blends. The alpha channel is in the format because
/// R8G8B8A8 has one, not because anything reads it (ADR-011).
inline constexpr std::uint8_t OPAQUE_ALPHA = 0xFF;

/// The colors this game refers to by name.
///
/// The values are the EGA default 16, a starting set rather than a constraint: the framebuffer
/// holds any of 2^24 colors, so a seventeenth name is a line in this file and nothing else
/// (ADR-011). The shaded and lit tone of one hue are paired explicitly, as a ColorPair per
/// material, because with no index there is no arithmetic to imply the pairing.
///
/// Pinned value-by-value by NeuronClientTests: these are the numbers every pixel on the screen is
/// one of, and a typo in one of them is a bug nobody would spot by looking at the screen.
inline constexpr Color BLACK = {0x00, 0x00, 0x00, OPAQUE_ALPHA};
inline constexpr Color BLUE = {0x00, 0x00, 0xAA, OPAQUE_ALPHA};
inline constexpr Color GREEN = {0x00, 0xAA, 0x00, OPAQUE_ALPHA};
inline constexpr Color CYAN = {0x00, 0xAA, 0xAA, OPAQUE_ALPHA};
inline constexpr Color RED = {0xAA, 0x00, 0x00, OPAQUE_ALPHA};
inline constexpr Color MAGENTA = {0xAA, 0x00, 0xAA, OPAQUE_ALPHA};
inline constexpr Color BROWN = {0xAA, 0x55, 0x00, OPAQUE_ALPHA};
inline constexpr Color LIGHT_GRAY = {0xAA, 0xAA, 0xAA, OPAQUE_ALPHA};
inline constexpr Color DARK_GRAY = {0x55, 0x55, 0x55, OPAQUE_ALPHA};
inline constexpr Color BRIGHT_BLUE = {0x55, 0x55, 0xFF, OPAQUE_ALPHA};
inline constexpr Color BRIGHT_GREEN = {0x55, 0xFF, 0x55, OPAQUE_ALPHA};
inline constexpr Color BRIGHT_CYAN = {0x55, 0xFF, 0xFF, OPAQUE_ALPHA};
inline constexpr Color BRIGHT_RED = {0xFF, 0x55, 0x55, OPAQUE_ALPHA};
inline constexpr Color BRIGHT_MAGENTA = {0xFF, 0x55, 0xFF, OPAQUE_ALPHA};
inline constexpr Color YELLOW = {0xFF, 0xFF, 0x55, OPAQUE_ALPHA};
inline constexpr Color WHITE = {0xFF, 0xFF, 0xFF, OPAQUE_ALPHA};

/// A surface's two tones: the one it is when the light misses it, and the one it is when the
/// light finds it. The mesh pass chooses between them per face and there is nothing in between
/// (ADR-012).
///
/// R8: a public aggregate, so plain fields. Authored into a mesh, so `{LIGHT_GRAY, WHITE}` reads
/// as what it is.
struct ColorPair
{
  Color shaded;
  Color lit;
};

/// A surface's four tones, brightest last: the shadow, the band the light grazes, the band it
/// finds, and the silhouette it gets past. The mesh pass chooses one per pixel by thresholding the
/// light, and there is nothing between them (ADR-105).
///
/// **Four rather than two because two was EGA's, not this game's** (ADR-104's argument, taken one
/// step further). The count is not a principle; every entry being a colour somebody named is.
///
/// R8: a public aggregate handed to the GPU. Authored as `{Shaded(c), HalfLit(c), c, Rimmed(c)}`,
/// which is why the order runs dark to bright — it reads as a ramp at the call site.
struct ColorRamp
{
  Color shaded;
  Color halfLit;
  Color lit;
  Color rim;
  /// The glint where the light bounces straight back at the eye. A SPOT rather than a band, which
  /// is why it earns a tone on a surface too small for a fifth step of the diffuse ramp -- it does
  /// not compete for band width (ADR-106). Equal to `lit` switches it off, which is how a flat-
  /// faced solid opts out, exactly as `rim` equal to `shaded` switches the silhouette off.
  Color glint;
};

} // namespace Neuron
