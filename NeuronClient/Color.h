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

} // namespace Neuron
