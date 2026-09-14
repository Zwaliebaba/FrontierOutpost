#pragma once

#include "MeshRenderer.h"

namespace Neuron
{

/// The Direct3D 12 half of the mesh pass: the pipeline, the vertex buffer, and the draw call that
/// drains a `MeshRenderer`.
///
/// **This is the only file in the mesh pass that names a graphics API** (ADR-075). The recorder
/// holds the geometry and every page and test talks to that; this holds the resources and is what
/// a second platform replaces.
///
/// It is the one pass in this renderer that TESTS AND WRITES DEPTH (ADR-103). The shape and text
/// passes draw in the order they were asked to, and painter's order is the whole of their
/// occlusion model; a ball in the world has a front and a back, and two balls have an order the
/// camera decides per pixel. The depth buffer `SceneTarget` clears each frame is written by nothing
/// else, so a ball is depth-tested against other balls and against nothing 2D -- everything flat
/// still layers by draw order, and the caller drains the shape recorder on either side of this.
class MeshBackend
{
public:
  void Create(ID3D12Device* _device);

  /// Copies everything the recorder has not handed over yet into slice `_frameIndex` of the upload
  /// heap, and draws it into the recorder's viewport. Does nothing when the recorder has nothing
  /// new.
  ///
  /// The viewport is the camera's pane, set for this draw and put back to the whole canvas after
  /// it, so the passes either side of this one see the viewport `SceneTarget::BeginScene` gave them.
  void Draw(ID3D12GraphicsCommandList* _commandList, std::uint32_t _frameIndex, MeshRenderer& _recorder);

private:
  /// A float4x4 and two padded float3s, which is what the mesh shaders' cbuffer declares: sixteen
  /// for the camera, four for the light, four for the eye the rim term is measured from.
  static constexpr std::uint32_t CONSTANT_COUNT = 24;

  void CreateVertexBuffer(ID3D12Device* _device);
  void CreatePipeline(ID3D12Device* _device);

  winrt::com_ptr<ID3D12Resource> m_vertexBuffer;
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;

  /// The upload heap, mapped for the life of the backend, and its FRAME_COUNT slices -- the same
  /// arrangement as `ShapeBackend`, for the same reason.
  MeshRenderer::MeshVertex* m_mappedVertices = nullptr;
};

} // namespace Neuron
