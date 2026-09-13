#pragma once

#include "Color.h"
#include "DescriptorHeap.h"
#include "Device.h"
#include "Font.h"

namespace Neuron
{

/// Draws text onto the screen, in any of the faces Font.h was baked with.
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
  [[nodiscard]] static constexpr std::uint32_t GlyphHeightPixels(std::uint32_t _scale = DEFAULT_SCALE, Face _face = DEFAULT_FACE) noexcept
  {
    return (static_cast<std::uint32_t>(FaceOf(_face).ascent) + FaceOf(_face).descent) * _scale;
  }

  /// The baseline-to-baseline distance the face was baked with.
  [[nodiscard]] static constexpr std::uint32_t LineHeightPixels(std::uint32_t _scale = DEFAULT_SCALE, Face _face = DEFAULT_FACE) noexcept
  {
    return FaceOf(_face).lineHeight * _scale;
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
  [[nodiscard]] static constexpr std::uint32_t AdvanceOf(char32_t _codepoint, std::uint32_t _scale = DEFAULT_SCALE,
                                                         Face _face = DEFAULT_FACE) noexcept
  {
    return GlyphOf(_codepoint, _face).advance * _scale;
  }

  /// How wide a string is, in screen pixels.
  ///
  /// A sum over the advances rather than a multiply by the string's length, which is why a
  /// proportional face needs no edit at any of the call sites that ask this. It is a NAMED
  /// measurement because every right-aligned and centred thing on the main page is laid out
  /// against it, and a stray `* 8` somewhere else is how those drift apart.
  [[nodiscard]] static constexpr std::uint32_t MeasurePixels(std::string_view _text, std::uint32_t _scale = DEFAULT_SCALE,
                                                             Face _face = DEFAULT_FACE) noexcept
  {
    std::uint32_t width = 0;
    for (std::size_t at = 0; at < _text.size();)
    {
      const Decoded decoded = DecodeUtf8(_text, at);
      width += AdvanceOf(decoded.codepoint, _scale, _face);
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
  [[nodiscard]] static constexpr std::size_t PrefixThatFits(std::string_view _text, std::uint32_t _widthPixels,
                                                            std::uint32_t _scale = DEFAULT_SCALE, Face _face = DEFAULT_FACE) noexcept
  {
    std::uint32_t used = 0;
    std::size_t fitted = 0;
    for (std::size_t at = 0; at < _text.size();)
    {
      const Decoded decoded = DecodeUtf8(_text, at);
      const std::uint32_t advance = AdvanceOf(decoded.codepoint, _scale, _face);
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
  [[nodiscard]] static std::vector<std::string> WrapToWidth(std::string_view _text, std::uint32_t _widthPixels,
                                                            std::uint32_t _scale = DEFAULT_SCALE, Face _face = DEFAULT_FACE);

  /// ONE COLUMN of a monospaced face, for the two things that are laid out in columns rather than
  /// measured: the join screen's caret, and the verdict box's two-character inset.
  ///
  /// It is the advance of a digit, which in a mono face is every glyph's advance. **Asking it of a
  /// PROPORTIONAL face is a category error** -- there is no column there -- so a caller that wants
  /// the width of something should call `MeasurePixels` on the something.
  [[nodiscard]] static constexpr std::uint32_t AdvancePixels(std::uint32_t _scale = DEFAULT_SCALE, Face _face = DEFAULT_FACE) noexcept
  {
    return AdvanceOf(U'0', _scale, _face);
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

  /// One texel of the baked atlas, which is what the upload copies and therefore what a test of
  /// the atlas can assert against without a device.
  [[nodiscard]] static constexpr std::uint8_t AtlasTexel(std::uint32_t _x, std::uint32_t _y) noexcept
  {
    return FONT_ATLAS[static_cast<std::size_t>(_y) * FONT_ATLAS_WIDTH + _x];
  }

  /// Uploads the atlas and builds the pipeline. Blocks until the copy has executed, because it
  /// runs once at startup and a startup that is a few milliseconds longer is not worth the
  /// machinery of tracking a pending upload.
  void Create(Device& _device, DescriptorHeap& _shaderVisibleHeap);

  /// Creates the renderer with NO DEVICE BEHIND IT: appended geometry lands in ordinary memory
  /// and `Flush` is refused.
  ///
  /// **This is the seam that makes a screen's LAYOUT testable.** Every page in this game builds its
  /// hit list while it draws -- `AddHit` sits beside the `FillRect` that put the button there, which
  /// is what stops the two drifting apart -- so a test that wants to press a button has to be able
  /// to run the draw. It could not: an append writes through a pointer into an upload heap, and
  /// without a device that pointer is null. Whoever wanted to test a tap had the choice of standing
  /// up D3D12 in a test DLL that CI runs on a machine with no GPU, or writing the layout out a
  /// second time in the test and asserting against a copy of the thing under test.
  ///
  /// A headless renderer records the same geometry into a vector instead. Nothing about the append
  /// path changes -- it is the same code writing to a different address -- so what a test drives is
  /// what ships.
  void CreateHeadless();

  /// Resets this frame's vertex slice. Every frame writes its own slice of the buffer, so the CPU
  /// never overwrites vertices the GPU is still reading. Also clears the clip rectangle.
  void BeginFrame(std::uint32_t _frameIndex) noexcept;

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
  void DrawText(std::int32_t _xPixels, std::int32_t _yPixels, std::string_view _text, const Color& _color,
                std::uint32_t _scale = DEFAULT_SCALE, Face _face = DEFAULT_FACE);

  /// Issues everything DrawText appended since BeginFrame as a single draw call.
  /// Draws everything recorded SINCE THE LAST FLUSH, and remembers where it stopped.
  ///
  /// **Called more than once a frame, it is what puts one layer over another.** The interface is
  /// two renderers (ADR-014), and each is one batch: every shape, then every glyph. Flushed once at
  /// the end of a frame that meant all text landed on top of all shapes whatever order they were
  /// recorded in -- so a panel drawn over the map covered the map's dots and lanes and left its
  /// LABELS floating on top of the panel, which is what a modal is not allowed to do.
  ///
  /// Draining rather than redrawing is the whole of the fix: the caller flushes both renderers
  /// between the world and the interface, and each flush draws only what is new.
  void Flush(ID3D12GraphicsCommandList* _commandList);

private:
  /// Position in screen pixels, the atlas texel to read, and the color to write. R8: a vertex is
  /// a public aggregate handed to the GPU, so plain fields.
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

  void CreateAtlas(Device& _device, DescriptorHeap& _shaderVisibleHeap);
  void CreateVertexBuffer(ID3D12Device* _device);
  void CreatePipeline(ID3D12Device* _device);

  winrt::com_ptr<ID3D12Resource> m_atlas;
  winrt::com_ptr<ID3D12Resource> m_vertices;
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;

  DescriptorHeap* m_shaderVisibleHeap = nullptr;
  std::uint32_t m_atlasSlot = 0;

  /// The whole vertex buffer, mapped for the life of the renderer. An upload heap is CPU-visible
  /// and GPU-readable; for a few hundred vertices a frame there is nothing a default-heap copy
  /// would buy.
  /// Where a headless renderer's geometry goes. Empty in the shipped path, where the vertices
  /// live in an upload heap the GPU reads directly.
  std::vector<TextVertex> m_headlessVertices;
  bool m_headless = false;

  TextVertex* m_mappedVertices = nullptr;
  std::uint32_t m_frameIndex = 0;
  std::uint32_t m_usedThisFrame = 0;
  /// How much of `m_usedThisFrame` has already been drawn this frame. See `Flush`.
  std::uint32_t m_flushedThisFrame = 0;

  /// The clip rectangle, in screen pixels. Defaults to everything, so a caller that never sets
  /// one is unaffected.
  float m_clipLeftPixels = 0.0F;
  float m_clipTopPixels = 0.0F;
  float m_clipRightPixels = 0.0F;
  float m_clipBottomPixels = 0.0F;
  bool m_clipping = false;
};

} // namespace Neuron
