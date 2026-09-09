#pragma once

#include "DescriptorHeap.h"
#include "Device.h"
#include "Font.h"

namespace Neuron
{

/// Draws 8x8 text into the palette index target.
///
/// Font.h holds 96 glyphs as one bit a pixel, 768 bytes, embedded in the binary (R13). This turns
/// that into a 768x8 R8_UINT atlas once at startup -- one texel per glyph pixel, 0 or 1 -- and
/// draws strings as quads that read it with Load(). No sampler, so a glyph texel is an exact 2x2
/// block of physical pixels at present scale 2 (ADR-001).
class FontRenderer
{
public:
  static constexpr std::uint32_t GLYPH_WIDTH_TEXELS = 8;
  static constexpr std::uint32_t GLYPH_HEIGHT_TEXELS = 8;

  /// Space through to the last printable ASCII character. Anything outside that range draws as a
  /// space rather than as whatever byte happened to follow the table.
  static constexpr std::uint32_t FIRST_CHARACTER = 32;
  static constexpr std::uint32_t GLYPH_COUNT = 96;

  /// One frame's worth of text. The game draws a status line, not a novel; overrunning this is a
  /// broken invariant rather than a case to grow into.
  static constexpr std::uint32_t MAX_CHARACTERS_PER_FRAME = 512;

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

  /// Resets this frame's vertex slice. Every frame writes its own slice of the buffer, so the CPU
  /// never overwrites vertices the GPU is still reading.
  void BeginFrame(std::uint32_t _frameIndex) noexcept;

  /// Appends one string at a position in 640x400 virtual texels, top-left of the first glyph.
  void DrawText(std::int32_t _xTexels, std::int32_t _yTexels, std::string_view _text, std::uint8_t _paletteIndex);

  /// Issues everything DrawText appended since BeginFrame as a single draw call.
  void Flush(ID3D12GraphicsCommandList* _commandList);

private:
  /// Position in virtual texels, the atlas texel to read, and the index to write. R8: a vertex is
  /// a public aggregate handed to the GPU, so plain fields.
  struct TextVertex
  {
    float positionXTexels;
    float positionYTexels;
    float glyphXTexels;
    float glyphYTexels;
    std::uint32_t paletteIndex;
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
  TextVertex* m_mappedVertices = nullptr;
  std::uint32_t m_frameIndex = 0;
  std::uint32_t m_usedThisFrame = 0;
};

} // namespace Neuron
