#pragma once

#include "ShapeRenderer.h"

namespace Neuron
{

/// The Direct3D 12 half of the shape pass: the pipeline, the vertex buffer, and the draw call that
/// drains a `ShapeRenderer`.
///
/// **This is the only file in the shape pass that names a graphics API** (ADR-075). The recorder
/// holds the geometry and every page and test talks to that; this holds the resources and is what
/// a second platform replaces. A Vulkan backend is a sibling of this file, and nothing above it
/// moves.
///
/// The split is along the seam the data already had. A recorder produces a span of vertices; a
/// backend copies that span somewhere the GPU can read and issues one draw. Nothing else crosses.
class ShapeBackend
{
public:
  void Create(ID3D12Device* _device);

  /// Copies everything the recorder has not handed over yet into slice `_frameIndex` of the upload
  /// heap, and draws it. Does nothing when the recorder has nothing new, which is what lets a
  /// caller drain between layers without paying for an empty draw.
  ///
  /// _recorder is taken by reference and not by const reference because taking the vertices is
  /// what marks them taken: a draw that could be issued twice would draw the same layer twice.
  void Draw(ID3D12GraphicsCommandList* _commandList, std::uint32_t _frameIndex, ShapeRenderer& _recorder);

private:
  /// Two floats for the canvas size in pixels, the same constant the text pass takes.
  static constexpr std::uint32_t CONSTANT_COUNT = 2;

  void CreateVertexBuffer(ID3D12Device* _device);
  void CreatePipeline(ID3D12Device* _device);

  winrt::com_ptr<ID3D12Resource> m_vertexBuffer;
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;

  /// The upload heap, mapped for the life of the backend, and its FRAME_COUNT slices -- which is
  /// what stops the CPU overwriting vertices a frame still in flight is reading.
  ///
  /// Where within a slice a batch goes is the recorder's answer, not a cursor kept here:
  /// `ShapeRenderer::Batch` carries it, so the backend holds no per-frame state at all and two
  /// backends draining the same recorder would agree.
  ShapeRenderer::ShapeVertex* m_mappedVertices = nullptr;
};

} // namespace Neuron
