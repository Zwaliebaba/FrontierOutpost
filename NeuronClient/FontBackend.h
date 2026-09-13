#pragma once

#include "FontRenderer.h"

namespace Neuron
{

class Device;
class DescriptorHeap;

/// The Direct3D 12 half of the text pass: the atlas as a GPU resource, the pipeline, the vertex
/// buffer, and the draw call that drains a `FontRenderer`.
///
/// **This is the only file in the text pass that names a graphics API** (ADR-075). The recorder
/// holds the atlas as DATA -- the glyph table, the metrics, the wrapping -- and every page and test
/// talks to that; this holds the texture and is what a second platform replaces.
class FontBackend
{
public:
  /// Uploads the atlas and builds the pipeline. Blocks until the copy has executed, because it
  /// runs once at startup and a startup that is a few milliseconds longer is not worth the
  /// machinery of tracking a pending upload.
  void Create(Device& _device, DescriptorHeap& _shaderVisibleHeap);

  /// Copies everything the recorder has not handed over yet into slice `_frameIndex` of the upload
  /// heap, and draws it. Does nothing when the recorder has nothing new.
  ///
  /// _recorder is taken by reference and not by const reference because taking the vertices is
  /// what marks them taken: a draw that could be issued twice would draw the same layer twice.
  void Draw(ID3D12GraphicsCommandList* _commandList, std::uint32_t _frameIndex, FontRenderer& _recorder);

private:
  /// Two floats for the canvas size in pixels.
  static constexpr std::uint32_t CONSTANT_COUNT = 2;

  void CreateAtlas(Device& _device, DescriptorHeap& _shaderVisibleHeap);
  void CreateVertexBuffer(ID3D12Device* _device);
  void CreatePipeline(ID3D12Device* _device);

  winrt::com_ptr<ID3D12Resource> m_atlas;
  winrt::com_ptr<ID3D12Resource> m_vertexBuffer;
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;

  /// Not owned. The one shader-visible heap the client has, and the slot the atlas's SRV went in.
  DescriptorHeap* m_shaderVisibleHeap = nullptr;
  std::uint32_t m_atlasSlot = 0;

  /// The upload heap, mapped for the life of the backend. An upload heap is CPU-visible and
  /// GPU-readable; for a few hundred vertices a frame there is nothing a default-heap copy would
  /// buy. Its FRAME_COUNT slices are what stop the CPU overwriting vertices a frame still in
  /// flight is reading, and where within one a batch goes is `FontRenderer::Batch`'s answer.
  FontRenderer::TextVertex* m_mappedVertices = nullptr;
};

} // namespace Neuron
