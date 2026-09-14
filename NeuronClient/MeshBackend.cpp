// MeshBackend.cpp -- the Direct3D 12 pipeline a MeshRenderer's triangles are drawn through
// (ADR-075, ADR-103). The geometry itself is MeshRenderer's, and it names no graphics API.

#include "pch.h"
#include "MeshBackend.h"

#include "D3D12Defaults.h"
#include "Device.h"
#include "SceneTarget.h"

#include "CompiledShaders/MeshPS.h"
#include "CompiledShaders/MeshVS.h"

namespace Neuron
{

void MeshBackend::Create(ID3D12Device* _device)
{
  CreateVertexBuffer(_device);
  CreatePipeline(_device);
}

void MeshBackend::CreateVertexBuffer(ID3D12Device* _device)
{
  const std::uint64_t sizeBytes =
    static_cast<std::uint64_t>(Device::FRAME_COUNT) * MeshRenderer::MAX_VERTICES_PER_FRAME * sizeof(MeshRenderer::MeshVertex);

  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC desc = BufferDesc(sizeBytes);
  winrt::check_hresult(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(m_vertexBuffer.put())));

  const D3D12_RANGE readNothing = {0, 0};
  winrt::check_hresult(m_vertexBuffer->Map(0, &readNothing, reinterpret_cast<void**>(&m_mappedVertices)));
}

void MeshBackend::CreatePipeline(ID3D12Device* _device)
{
  std::array<D3D12_ROOT_PARAMETER1, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  // Both stages read the one block: the vertex shader wants the camera and the pixel shader wants
  // the light, and one block both can see is simpler than two that have to agree on an offset.
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[0].Constants.ShaderRegister = 0;
  parameters[0].Constants.RegisterSpace = 0;
  parameters[0].Constants.Num32BitValues = CONSTANT_COUNT;

  const D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {
    .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
    .Desc_1_1 = {.NumParameters = static_cast<UINT>(parameters.size()),
                 .pParameters = parameters.data(),
                 .NumStaticSamplers = 0,
                 .pStaticSamplers = nullptr,
                 .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT},
  };

  winrt::com_ptr<ID3DBlob> serialized;
  winrt::com_ptr<ID3DBlob> errors;
  const HRESULT serializeResult = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, serialized.put(), errors.put());
  if (FAILED(serializeResult) && errors)
  {
    Fatal("Mesh root signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
  }
  winrt::check_hresult(serializeResult);
  winrt::check_hresult(
    _device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  const std::array<D3D12_INPUT_ELEMENT_DESC, 4> inputLayout = {
    D3D12_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshRenderer::MeshVertex, x),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshRenderer::MeshVertex, nx),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(MeshRenderer::MeshVertex, litColor),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(MeshRenderer::MeshVertex, darkColor),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = DefaultGraphicsPipeline();
  pipelineDesc.pRootSignature = m_rootSignature.get();
  pipelineDesc.VS = {g_MeshVS, sizeof(g_MeshVS)};
  pipelineDesc.PS = {g_MeshPS, sizeof(g_MeshPS)};
  pipelineDesc.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
  // Scene, not interface: a solid has a back the camera cannot see and a front that is nearer
  // than the ball behind it, so the back is culled and the front is depth-tested (ADR-103). The
  // recorder winds its faces counter-clockwise seen from outside, which is the right-handed
  // convention the world is in, so the rasterizer is told that is a front face. Opaque, unsampled
  // and unblended, like every pass that is not the interface (ADR-014).
  pipelineDesc.RasterizerState = SolidRasterizer(D3D12_CULL_MODE_BACK);
  pipelineDesc.RasterizerState.FrontCounterClockwise = TRUE;
  pipelineDesc.DepthStencilState = DepthState(true);
  pipelineDesc.DSVFormat = SceneTarget::DEPTH_FORMAT;
  pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  winrt::check_hresult(_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_pipeline.put())));
}

void MeshBackend::Draw(ID3D12GraphicsCommandList* _commandList, std::uint32_t _frameIndex, MeshRenderer& _recorder)
{
  ASSERT_TEXT(m_pipeline != nullptr, L"Drawing through a MeshBackend that was never created.");

  const MeshRenderer::Batch batch = _recorder.TakeUnflushed();
  if (batch.vertices.empty())
  {
    return;
  }

  ASSERT_TEXT(batch.firstVertex + batch.vertices.size() <= MeshRenderer::MAX_VERTICES_PER_FRAME,
              L"A mesh batch runs past the end of its frame's slice.");

  MeshRenderer::MeshVertex* slice = m_mappedVertices + static_cast<std::size_t>(_frameIndex) * MeshRenderer::MAX_VERTICES_PER_FRAME;
  std::ranges::copy(batch.vertices, slice + batch.firstVertex);

  const std::uint64_t sliceOffsetBytes =
    static_cast<std::uint64_t>(_frameIndex) * MeshRenderer::MAX_VERTICES_PER_FRAME * sizeof(MeshRenderer::MeshVertex);
  const auto pastThisBatch = static_cast<std::uint32_t>(batch.firstVertex + batch.vertices.size());

  D3D12_VERTEX_BUFFER_VIEW vertexView = {};
  vertexView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress() + sliceOffsetBytes;
  vertexView.SizeInBytes = pastThisBatch * sizeof(MeshRenderer::MeshVertex);
  vertexView.StrideInBytes = sizeof(MeshRenderer::MeshVertex);

  // The camera's pane, not the canvas: clip space is the camera's, and the camera projects into the
  // rectangle the page gave it. Put back afterwards so the next pass draws where BeginScene said.
  const MeshRenderer::View& view = _recorder.CurrentView();
  const D3D12_VIEWPORT pane = {view.viewportXPixels, view.viewportYPixels, view.viewportWidthPixels, view.viewportHeightPixels, 0.0F, 1.0F};
  const D3D12_VIEWPORT canvas = {0.0F, 0.0F, static_cast<float>(SceneTarget::WIDTH_PIXELS), static_cast<float>(SceneTarget::HEIGHT_PIXELS),
                                 0.0F, 1.0F};
  _commandList->RSSetViewports(1, &pane);

  _commandList->SetGraphicsRootSignature(m_rootSignature.get());
  _commandList->SetPipelineState(m_pipeline.get());

  std::array<float, CONSTANT_COUNT> constants = {};
  std::ranges::copy(view.viewProjection, constants.begin());
  constants[16] = view.lightDirection.x;
  constants[17] = view.lightDirection.y;
  constants[18] = view.lightDirection.z;
  constants[19] = 0.0F;
  _commandList->SetGraphicsRoot32BitConstants(0, CONSTANT_COUNT, constants.data(), 0);

  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->IASetVertexBuffers(0, 1, &vertexView);
  _commandList->DrawInstanced(static_cast<std::uint32_t>(batch.vertices.size()), 1, batch.firstVertex, 0);

  _commandList->RSSetViewports(1, &canvas);
}

} // namespace Neuron
