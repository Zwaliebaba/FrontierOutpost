#pragma once

#include "Color.h"
#include "DescriptorHeap.h"
#include "Device.h"
#include "Font.h"

namespace Neuron
{

/// Draws 8x8 text onto the screen.
///
/// Font.h holds 96 glyphs as one bit a pixel, 768 bytes, embedded in the binary (R13). This turns
/// that into a 768x8 R8_UINT atlas once at startup -- one texel per glyph pixel, 0 or 1 -- and
/// draws strings as quads that read it with Load(). No sampler anywhere, so a glyph texel is an
/// exact GLYPH_SCALE x GLYPH_SCALE block of screen pixels with nothing to filter (ADR-011).
class FontRenderer
{
public:
  static constexpr std::uint32_t GLYPH_WIDTH_TEXELS = 8;
  static constexpr std::uint32_t GLYPH_HEIGHT_TEXELS = 8;

  /// How many screen pixels a glyph texel occupies, on both axes, when a caller does not say.
  ///
  /// The scale is a PER-CALL argument rather than the compile-time constant it was until ADR-014
  /// on 2026-09-10, because the UI design has now settled the question ADR-013 left
  /// open: 1x (8px) everywhere on the main page, 2x (16px) for the lock countdown and nothing
  /// else (ADR-014). One number could not say that.
  ///
  /// It is always a WHOLE number, and there is no sampler on the path, so the enlargement is an
  /// exact block of pixels rather than a filtered one.
  static constexpr std::uint32_t DEFAULT_SCALE = 1;
  static constexpr std::uint32_t COUNTDOWN_SCALE = 2;

  /// What one character advances the cursor by, and how tall a line is, at a given scale.
  [[nodiscard]] static constexpr std::uint32_t AdvancePixels(std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    return GLYPH_WIDTH_TEXELS * _scale;
  }
  [[nodiscard]] static constexpr std::uint32_t GlyphHeightPixels(std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    return GLYPH_HEIGHT_TEXELS * _scale;
  }

  /// How wide a string is, in screen pixels. The font is fixed-pitch, so this is a multiply --
  /// but it is a named multiply, because every right-aligned and centred thing on the main page
  /// is laid out against it and a stray `* 8` somewhere else is how those drift apart.
  [[nodiscard]] static constexpr std::uint32_t MeasurePixels(std::string_view _text, std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    return static_cast<std::uint32_t>(_text.size()) * AdvancePixels(_scale);
  }

  /// The most characters that fit in a width. Used by the digest and the orders rail, whose copy
  /// is wrapped to the rail rather than truncated (ADR-014).
  [[nodiscard]] static constexpr std::size_t FitCharacters(std::uint32_t _widthPixels, std::uint32_t _scale = DEFAULT_SCALE) noexcept
  {
    return _widthPixels / AdvancePixels(_scale);
  }

  /// Word-wraps to a character count, breaking on spaces and hard-breaking a word longer than the
  /// line.
  ///
  /// It lives with the font rather than with the screen that needed it, because wrapping to a
  /// FIXED-PITCH font is arithmetic on the font's own advance -- there is no measurement pass and
  /// no kerning, so "how many characters fit" is the whole problem and this class is what knows
  /// it. It is also the piece most likely to be wrong, and here it is reachable from a test
  /// suite; in the executable it would not be.
  [[nodiscard]] static std::vector<std::string> Wrap(std::string_view _text, std::size_t _maxCharacters);

  /// The same, given a width in pixels rather than a character count.
  [[nodiscard]] static std::vector<std::string> WrapToWidth(std::string_view _text, std::uint32_t _widthPixels,
                                                            std::uint32_t _scale = DEFAULT_SCALE)
  {
    return Wrap(_text, FitCharacters(_widthPixels, _scale));
  }

  /// Space through to the last printable ASCII character. Anything outside that range draws as a
  /// space rather than as whatever byte happened to follow the table.
  static constexpr std::uint32_t FIRST_CHARACTER = 32;
  static constexpr std::uint32_t GLYPH_COUNT = 96;

  /// One frame's worth of text.
  ///
  /// It was 512 when the client drew a two-line status display over a 3D scene. The main page is
  /// a text interface -- a seven-event digest, three columns of orders, and every label on the
  /// map -- and a full frame of it measures a little over 1,600 characters, so 512 was not a
  /// budget it exceeded but one it was never sized for. 4,096 leaves room for a digest twice as
  /// long as any tick has produced; overrunning it is still a broken invariant rather than a case
  /// to grow into.
  static constexpr std::uint32_t MAX_CHARACTERS_PER_FRAME = 4096;

  /// Where a character sits in Font.h's table. Anything outside it -- control codes, high bytes,
  /// a stray UTF-8 continuation byte -- maps to the space at index 0, so a bad string draws
  /// blanks instead of reading past the end of a 768-byte array.
  [[nodiscard]] static constexpr std::uint32_t GlyphIndex(char _character) noexcept
  {
    const auto code = static_cast<std::uint32_t>(static_cast<std::uint8_t>(_character));
    return (code >= FIRST_CHARACTER && code < FIRST_CHARACTER + GLYPH_COUNT) ? code - FIRST_CHARACTER : 0;
  }

  /// One row of one glyph: eight pixels, most significant bit leftmost, exactly as Font.h stores
  /// them. This is the lookup the atlas is built from, so a test of it is a test of the atlas.
  [[nodiscard]] static constexpr std::uint8_t GlyphRow(char _character, std::uint32_t _row) noexcept
  {
    return FONT_DATA[static_cast<std::size_t>(GlyphIndex(_character)) * GLYPH_HEIGHT_TEXELS + _row];
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
                std::uint32_t _scale = DEFAULT_SCALE);

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
