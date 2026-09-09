#pragma once

namespace Neuron
{

/// Sixteen colors, and the screen never has a seventeenth (Design/README.md section 1).
inline constexpr std::uint32_t PALETTE_SIZE = 16;

/// The EGA default 16, 0x00RRGGBB, in hardware index order 0-15.
///
/// The order is not decorative. Index n and index n+8 are the dark and bright variant of the same
/// hue, which is what makes two-tone flat shading possible without a second palette: a face
/// authored as color n lights to n+8 and shades to n (ADR-002). The one place that pairing is a
/// lie is 6/14 -- EGA's brown and yellow rather than dark and bright yellow -- and that is a
/// property of the hardware palette, not a mistake here.
///
/// Owner decision, 2026-09-09 (Design/Plans MVP-01 section 2). Pinned value-by-value by
/// NeuronClientTests: this table is a wire format in all but name, and a typo in it is a bug
/// nobody would spot by looking at the screen.
inline constexpr std::array<std::uint32_t, PALETTE_SIZE> EGA_PALETTE = {
  0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
  0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

/// The palette indices this game refers to by name. Enumerators, so PascalCase (AGENTS.md R3).
enum class PaletteIndex : std::uint8_t
{
  Black = 0,
  Blue = 1,
  Green = 2,
  Cyan = 3,
  Red = 4,
  Magenta = 5,
  Brown = 6,
  LightGray = 7,
  DarkGray = 8,
  BrightBlue = 9,
  BrightGreen = 10,
  BrightCyan = 11,
  BrightRed = 12,
  BrightMagenta = 13,
  Yellow = 14,
  White = 15
};

[[nodiscard]] inline constexpr std::uint8_t ToIndex(PaletteIndex _color) noexcept
{
  return static_cast<std::uint8_t>(_color);
}

} // namespace Neuron
