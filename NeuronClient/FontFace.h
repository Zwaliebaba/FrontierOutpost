// NeuronClient/FontFace.h
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

/// One glyph as a FontFace renders it: 8-bit coverage, the top row first, and where it sits.
struct GlyphBitmap
{
  std::uint32_t widthPixels = 0;
  std::uint32_t heightPixels = 0;
  std::int32_t leftPixels = 0; // from the pen to the bitmap's left edge
  std::int32_t topPixels = 0;  // from the baseline up to the bitmap's top edge
  float advancePixels = 0.0f;  // from this glyph's pen to the next's, before kerning
  std::vector<std::uint8_t> coverage;
};

/// A font file's first face at one pixel size, rasterized by DirectWrite (Design/ADR/ADR-010).
/// Faces come from files, never from the fonts installed on the system.
class FontFace
{
public:
  /// Opens _pathUtf8's first face at an em height of _emSizePixels. On failure returns false and
  /// says why in _error.
  [[nodiscard]] static bool Open(std::string_view _pathUtf8, float _emSizePixels, FontFace& _outFace, std::string& _error);

  FontFace();
  ~FontFace();
  FontFace(FontFace&& _other) noexcept;
  FontFace& operator=(FontFace&& _other) noexcept;
  FontFace(const FontFace&) = delete;
  FontFace& operator=(const FontFace&) = delete;

  /// The glyph for a Unicode code point, or 0 when the face has none.
  [[nodiscard]] std::uint16_t GlyphIndex(char32_t _codePoint) const;

  /// Renders a glyph's coverage, with its bearing and advance. On failure returns false and says
  /// why in _error.
  [[nodiscard]] bool RenderGlyph(std::uint16_t _glyphIndex, GlyphBitmap& _outBitmap, std::string& _error) const;

  /// The change to the first glyph's advance when the second follows it, from the face's kern
  /// table; 0 when the table has no such pair, or the face has no table.
  [[nodiscard]] float KerningPixels(std::uint16_t _leftGlyph, std::uint16_t _rightGlyph) const;

private:
  struct Native;
  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
