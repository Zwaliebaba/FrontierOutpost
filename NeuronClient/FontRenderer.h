#pragma once

#include "Color.h"
#include "Font.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

/// RECORDS text in canvas pixels, in any of the faces Font.h was baked with. Drawing it is
/// `FontBackend`.
///
/// **This header names no graphics API and no operating system, and that is the point of the
/// split** (ADR-075). It keeps the atlas as DATA -- the glyph table, the metrics, the wrapping, the
/// UTF-8 decoding, the clip rectangle -- and a `FontBackend` holds the atlas as a GPU RESOURCE and
/// drains the quads. A Vulkan or Metal build is a second backend behind this same recorder.
///
/// Font.h is GENERATED (ADR-073): `Build/BakeFont.py` rasterizes the faces offline and writes one
/// coverage atlas, one glyph table and one face table as `constexpr` arrays, which are embedded in
/// the binary exactly as the hand-typed 8x8 font was (R13). This uploads the atlas once at startup
/// and draws strings as quads that read it with Load(). No sampler anywhere, so a glyph texel is
/// an exact block of screen pixels with nothing to filter (ADR-011).
///
/// **A glyph is placed against a BASELINE, not against the top-left of a box.** `DrawText` takes
/// the top of the line, and each glyph lands at `ascent - bearingY` below it. The 8x8 font had no
/// baseline and no bearings, which is why it could be drawn from a corner; a real face cannot be,
/// and `y` keeps meaning what it meant so that no caller has to care.
class FontRenderer
{
public:
  /// How many screen pixels a glyph texel occupies, on both axes, when a caller does not say.
  ///
  /// The scale is a PER-CALL argument rather than a compile-time constant, because the UI design
  /// settled the question ADR-013 left open: 1x (8px) everywhere on the main page, 2x (16px) for
  /// the lock countdown and nothing else (ADR-014). One number could not say that.
  ///
  /// It is always a WHOLE number, and there is no sampler on the path, so the enlargement is an
  /// exact block of pixels rather than a filtered one.
  static constexpr std::uint32_t DEFAULT_SCALE = 1;
  static constexpr std::uint32_t COUNTDOWN_SCALE = 2;

  /// Which cut a string is drawn in (ADR-074). Mono carries data, sans carries sentences.
  ///
  /// **The face comes BEFORE the scale in every signature that takes both**, which is the reverse
  /// of the order they arrived in. Nearly every call site names a face and takes the default
  /// scale -- five in the whole client name a scale -- so the other order made the common case
  /// write `DEFAULT_SCALE` purely to reach past it. Argument ordering is a readability question
  /// eighty times over and a typing question five times, and this is which way that falls.
  static constexpr Face DEFAULT_FACE = Face::MonoRegular;

  /// The baked metrics of one face.
  [[nodiscard]] static constexpr const FontFace& FaceOf(Face _face) noexcept
  {
    return FONT_FACES[static_cast<std::size_t>(_face)];
  }

  /// One glyph of one face, or the face's first glyph when the codepoint was not baked.
  ///
  /// The fallback keeps the property the `code - 32` table had: a string carrying something the
  /// font does not know draws a blank rather than reading past the end of an array. The subset
  /// always starts at space, so the first glyph IS a blank.
  [[nodiscard]] static constexpr const FontGlyph& GlyphOf(char32_t _codepoint, Face _face) noexcept
  {
    const FontFace& face = FaceOf(_face);
    std::size_t low = face.firstGlyph;
    std::size_t high = low + face.glyphCount;
    while (low < high)
    {
      const std::size_t middle = low + (high - low) / 2;
      if (FONT_GLYPHS[middle].codepoint < _codepoint)
      {
        low = middle + 1;
      }
      else
      {
        high = middle;
      }
    }
    const bool found = low < static_cast<std::size_t>(face.firstGlyph) + face.glyphCount &&
                       FONT_GLYPHS[low].codepoint == static_cast<std::uint32_t>(_codepoint);
    return FONT_GLYPHS[found ? low : face.firstGlyph];
  }

  /// What one line of a face occupies vertically, and what a glyph box is measured against.
  ///
  /// Ascent plus descent rather than the face's line height, because this is what the CLIP box and
  /// every vertical centring on the four screens is built from, and for the 8x8 font it has to
  /// come out at exactly eight or a clipped map label would clip somewhere new.
  [[nodiscard]] static constexpr std::uint32_t GlyphHeightPixels(Face _face = DEFAULT_FACE, std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    return (static_cast<std::uint32_t>(FaceOf(_face).ascent) + FaceOf(_face).descent) * _scale;
  }

  /// The baseline-to-baseline distance to set this face at. **Never less than the glyph box.**
  ///
  /// Plex at 12px is the case that makes the floor necessary: it reports an ascent of 13, a descent
  /// of 4 and a line height of 16, so the box it asks for is a pixel TALLER than the advance it
  /// recommends. That is legal -- the face carries a negative line gap, and hinted at this size the
  /// extremes it reserves are rarely both occupied -- but it makes consecutive lines overlap, and
  /// `GlyphHeightPixels` is what the clip box and every vertical centring are built from. Two
  /// numbers that disagree about how tall a line is, used one each by the layout and the clipper,
  /// is the shape of a bug nobody finds by reading either one.
  ///
  /// So the renderer guarantees the invariant instead of asking every caller to remember it: a line
  /// always has room for its own box. For a face whose line height already exceeds its box -- which
  /// is the normal case -- this returns exactly what was baked.
  [[nodiscard]] static constexpr std::uint32_t LineHeightPixels(Face _face = DEFAULT_FACE, std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    const std::uint32_t baked = FaceOf(_face).lineHeight * _scale;
    const std::uint32_t box = GlyphHeightPixels(_face, _scale);
    return baked < box ? box : baked;
  }

  /// Decodes one UTF-8 codepoint, returning it and how many bytes it took.
  ///
  /// Every `std::string` in this tree is UTF-8 (`NeuronCore/Text.h`), which nothing needed to know
  /// while the font was 96 bytes of ASCII and everything needs to know now that `·` and `→` are
  /// glyphs rather than substitutions. A malformed byte is consumed as one replacement, so a bad
  /// string cannot stall the cursor.
  struct Decoded
  {
    char32_t codepoint;
    std::size_t bytes;
  };
  [[nodiscard]] static constexpr Decoded DecodeUtf8(std::string_view _text, std::size_t _at) noexcept
  {
    const auto lead = static_cast<std::uint8_t>(_text[_at]);
    const auto continuation = [&](std::size_t _offset) constexpr noexcept -> char32_t
    { return (_at + _offset < _text.size()) ? static_cast<char32_t>(static_cast<std::uint8_t>(_text[_at + _offset]) & 0x3FU) : 0U; };

    if (lead < 0x80U)
    {
      return {static_cast<char32_t>(lead), 1};
    }
    if ((lead & 0xE0U) == 0xC0U && _at + 1 < _text.size())
    {
      return {((static_cast<char32_t>(lead) & 0x1FU) << 6) | continuation(1), 2};
    }
    if ((lead & 0xF0U) == 0xE0U && _at + 2 < _text.size())
    {
      return {((static_cast<char32_t>(lead) & 0x0FU) << 12) | (continuation(1) << 6) | continuation(2), 3};
    }
    if ((lead & 0xF8U) == 0xF0U && _at + 3 < _text.size())
    {
      return {((static_cast<char32_t>(lead) & 0x07U) << 18) | (continuation(1) << 12) | (continuation(2) << 6) | continuation(3), 4};
    }
    return {0xFFFDU, 1};
  }

  /// What ONE codepoint advances the cursor by. The single place that reads the advance table.
  [[nodiscard]] static constexpr std::uint32_t AdvanceOf(char32_t _codepoint, Face _face = DEFAULT_FACE,
                                                         std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    return GlyphOf(_codepoint, _face).advance * _scale;
  }

  /// How wide a string is, in screen pixels.
  ///
  /// A sum over the advances rather than a multiply by the string's length, which is why a
  /// proportional face needs no edit at any of the call sites that ask this. It is a NAMED
  /// measurement because every right-aligned and centred thing on the main page is laid out
  /// against it, and a stray `* 8` somewhere else is how those drift apart.
  [[nodiscard]] static constexpr std::uint32_t MeasurePixels(std::string_view _text, Face _face = DEFAULT_FACE,
                                                             std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    std::uint32_t width = 0;
    for (std::size_t at = 0; at < _text.size();)
    {
      const Decoded decoded = DecodeUtf8(_text, at);
      width += AdvanceOf(decoded.codepoint, _face, _scale);
      at += decoded.bytes;
    }
    return width;
  }

  /// How many characters of a string fit in a width, counting from the front.
  ///
  /// **It accumulates one advance at a time rather than dividing**, which today is the same answer
  /// arrived at the long way -- the font is fixed-pitch, so the sum is a multiply. It is written
  /// as a scan because the advance stops being one number when the face does (ADR-073), and a
  /// division is the shape that would have to be found and rewritten then. There is nothing to
  /// find here.
  [[nodiscard]] static constexpr std::size_t PrefixThatFits(std::string_view _text, std::uint32_t _widthPixels, Face _face = DEFAULT_FACE,
                                                            std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    std::uint32_t used = 0;
    std::size_t fitted = 0;
    for (std::size_t at = 0; at < _text.size();)
    {
      const Decoded decoded = DecodeUtf8(_text, at);
      const std::uint32_t advance = AdvanceOf(decoded.codepoint, _face, _scale);
      if (used + advance > _widthPixels)
      {
        break;
      }
      used += advance;
      at += decoded.bytes;
      // BYTES, not codepoints: every caller feeds the answer straight back to `substr`, and a
      // count of characters would cut a multi-byte glyph in half the first time the copy carries
      // one of the five ADR-014 had to substitute.
      fitted = at;
    }
    return fitted;
  }

  /// Word-wraps to a PIXEL WIDTH, breaking on spaces and hard-breaking a word longer than the
  /// line. The only wrap entry point; the digest and the orders rail wrap their copy to the rail
  /// rather than truncating it (ADR-014).
  ///
  /// It lives with the font rather than with the screen that needed it, because wrapping is
  /// arithmetic on the font's own advances and this class is what knows them. It is also the piece
  /// most likely to be wrong, and here it is reachable from a test suite; in the executable it
  /// would not be.
  ///
  /// **A width, not a character count.** It took a count until 2026-09-13, which was the same
  /// question while every glyph was eight pixels wide and stops being it the moment one is not
  /// (ADR-073). A caller that knows a rail is 254 pixels wide now says so, instead of dividing by
  /// eight somewhere the font cannot see.
  [[nodiscard]] static std::vector<std::string> WrapToWidth(std::string_view _text, std::uint32_t _widthPixels, Face _face = DEFAULT_FACE,
                                                            std::uint32_t _scale = DEFAULT_SCALE);

  /// ONE COLUMN of a monospaced face, for the two things that are laid out in columns rather than
  /// measured: the join screen's caret, and the verdict box's two-character inset.
  ///
  /// It is the advance of a digit, which in a mono face is every glyph's advance. **Asking it of a
  /// PROPORTIONAL face is a category error** -- there is no column there -- so a caller that wants
  /// the width of something should call `MeasurePixels` on the something.
  [[nodiscard]] static constexpr std::uint32_t AdvancePixels(Face _face = DEFAULT_FACE, std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    return AdvanceOf(U'0', _face, _scale);
  }

  /// One frame's worth of text.
  ///
  /// It was 512 when the client drew a two-line status display over a 3D scene. The main page is
  /// a text interface -- a seven-event digest, three columns of orders, and every label on the
  /// map -- and a full frame of it measures a little over 1,600 characters, so 512 was not a
  /// budget it exceeded but one it was never sized for. 4,096 leaves room for a digest twice as
  /// long as any tick has produced; overrunning it is still a broken invariant rather than a case
  /// to grow into.
  static constexpr std::uint32_t MAX_CHARACTERS_PER_FRAME = 4096;

  /// Position in canvas pixels, the atlas texel to read, and the color to write. R8: a vertex is
  /// a public aggregate handed to the GPU, so plain fields -- and public, because a backend is what
  /// hands it over.
  struct TextVertex
  {
    float positionXPixels;
    float positionYPixels;
    float glyphXTexels;
    float glyphYTexels;
    /// Packed by Pack(), read back by an R8G8B8A8_UNORM input element.
    std::uint32_t color;
  };

  static constexpr std::uint32_t VERTICES_PER_GLYPH = 6;
  static constexpr std::uint32_t MAX_VERTICES_PER_FRAME = MAX_CHARACTERS_PER_FRAME * VERTICES_PER_GLYPH;

  /// One batch of recorded vertices, and where it sits in this frame's recording. See
  /// `ShapeRenderer::Batch`: the index is what keeps a second layer off vertices the GPU has been
  /// told to read and has not read yet.
  struct Batch
  {
    std::span<const TextVertex> vertices;
    std::uint32_t firstVertex;
  };

  /// One texel of the baked atlas, which is what the upload copies and therefore what a test of
  /// the atlas can assert against without a device.
  [[nodiscard]] static constexpr std::uint8_t AtlasTexel(std::uint32_t _x, std::uint32_t _y) noexcept
  {
    return FONT_ATLAS[static_cast<std::size_t>(_y) * FONT_ATLAS_WIDTH + _x];
  }

  /// **Recording is all this class does, which is what makes a screen's layout testable without a
  /// GPU** (ADR-041). Every page builds its hit list while it draws -- `AddHit` sits beside the
  /// `FillRect` that put the button there, which is what stops the two drifting apart -- so a test
  /// that wants to press a button has to run the draw. It can.

  /// One string this renderer was asked to draw, and the face it was asked for.
  ///
  /// The geometry cannot answer this: by the time a string is vertices it is glyph boxes with no
  /// word boundaries and no face, and recovering either from an atlas coordinate would be a
  /// parser. The string is captured on the way IN instead, which is where it is still a sentence.
  struct DrawnString
  {
    std::string text;
    Face face;
  };

  /// Every string this renderer drew since `BeginFrame`, in draw order.
  ///
  /// Recorded on every frame, drawn or not. It costs a few hundred small string copies on a client
  /// that redraws only when something changed (ADR-047), and that is cheaper than a second code
  /// path that the tests would then be the only user of.
  ///
  /// **This exists so ADR-074's face rule can be a test rather than a convention.** The rule --
  /// data is mono, sentences are sans -- is applied at eighty-odd call sites and would otherwise
  /// have to be re-checked by reading all of them every time a line of copy changes.
  [[nodiscard]] const std::vector<DrawnString>& DrawnStrings() const noexcept
  {
    return m_drawnStrings;
  }

  /// Starts a frame's recording over, and clears the clip rectangle. Not noexcept: the first call
  /// reserves the vector, and an allocation that fails is a thing to report rather than a
  /// std::terminate (Debug.h).
  ///
  /// It takes no frame index, for the reason `ShapeRenderer::BeginFrame` does not.
  void BeginFrame();

  /// Confines subsequent text to a rectangle, by GLYPH: a glyph that does not fit entirely inside
  /// is not drawn at all.
  ///
  /// It exists because the map got a camera. Every other pane's text is laid out inside its own
  /// pane by construction and could never leave it, but a projected label can land anywhere on
  /// the screen -- a system swung behind the viewer puts its name across the digest (ADR-017).
  ///
  /// Whole glyphs rather than partial ones because the alternative is a glyph cut down the middle,
  /// which on an 8x8 font is two or three lit columns of something unreadable. A label that runs
  /// off the pane loses its last letter cleanly instead.
  void SetClipRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels) noexcept;
  void ClearClipRect() noexcept;

  /// Appends one string at a position in screen pixels, top-left of the first glyph.
  ///
  /// The origin is whole pixels by type, not by convention: a glyph on a half pixel is the one
  /// way this renderer could produce a soft edge, and an integer parameter makes that
  /// unreachable rather than merely discouraged.
  void DrawText(std::int32_t _xPixels, std::int32_t _yPixels, std::string_view _text, const Color& _color, Face _face = DEFAULT_FACE,
                std::uint32_t _scale = DEFAULT_SCALE);

  /// Everything recorded since the last take, and marks it taken. Empty when nothing is new.
  ///
  /// **Called more than once a frame, it is what puts one layer over another.** The interface is
  /// two renderers (ADR-014), and each is one batch: every shape, then every glyph. Drained once at
  /// the end of a frame, all text landed on top of all shapes whatever order they were recorded in
  /// -- so a panel drawn over the map covered the map's dots and lanes and left its LABELS floating
  /// on top of the panel, which is what a modal is not allowed to do.
  ///
  /// The span points into this recorder and stays valid until the next `BeginFrame`, which is long
  /// enough for a backend to copy it and no longer.
  [[nodiscard]] Batch TakeUnflushed() noexcept;

private:
  /// This frame's geometry, and the ONE place an append lands. Reserved once to
  /// MAX_VERTICES_PER_FRAME and cleared rather than freed, so a frame's recording never allocates.
  std::vector<TextVertex> m_vertices;

  /// What this renderer was ASKED to draw, beside what it drew. See `DrawnStrings`.
  std::vector<DrawnString> m_drawnStrings;

  /// How much of `m_vertices` has already been taken this frame. See `TakeUnflushed`.
  std::size_t m_takenThisFrame = 0;

  /// The clip rectangle, in screen pixels. Defaults to everything, so a caller that never sets
  /// one is unaffected.
  float m_clipLeftPixels = 0.0F;
  float m_clipTopPixels = 0.0F;
  float m_clipRightPixels = 0.0F;
  float m_clipBottomPixels = 0.0F;
  bool m_clipping = false;
};

} // namespace Neuron
