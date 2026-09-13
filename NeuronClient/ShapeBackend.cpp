// ShapeBackend.cpp -- the Direct3D 12 pipeline a ShapeRenderer's vertices are drawn through
// (ADR-075). The geometry itself is ShapeRenderer's, and it names no graphics API.

#include "pch.h"
#include "ShapeBackend.h"

#include "D3D12Defaults.h"
#include "Device.h"
#include "SceneTarget.h"

#include "CompiledShaders/ShapePS.h"
#include "CompiledShaders/ShapeVS.h"

namespace Neuron
{

void ShapeBackend::Create(ID3D12Device* _device)
{
  CreateVertexBuffer(_device);
  CreatePipeline(_device);
}

void ShapeBackend::CreateVertexBuffer(ID3D12Device* _device)
{
  const std::uint64_t sizeBytes =
    static_cast<std::uint64_t>(Device::FRAME_COUNT) * ShapeRenderer::MAX_VERTICES_PER_FRAME * sizeof(ShapeRenderer::ShapeVertex);

  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC desc = BufferDesc(sizeBytes);
  winrt::check_hresult(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(m_vertexBuffer.put())));

  const D3D12_RANGE readNothing = {0, 0};
  winrt::check_hresult(m_vertexBuffer->Map(0, &readNothing, reinterpret_cast<void**>(&m_mappedVertices)));
}

void ShapeBackend::CreatePipeline(ID3D12Device* _device)
{
  std::array<D3D12_ROOT_PARAMETER1, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
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
    Fatal("Shape root signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
  }
  winrt::check_hresult(serializeResult);
  winrt::check_hresult(
    _device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  const std::array<D3D12_INPUT_ELEMENT_DESC, 2> inputLayout = {
    D3D12_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ShapeRenderer::ShapeVertex, positionXPixels),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ShapeRenderer::ShapeVertex, color),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = DefaultGraphicsPipeline();
  pipelineDesc.pRootSignature = m_rootSignature.get();
  pipelineDesc.VS = {g_ShapeVS, sizeof(g_ShapeVS)};
  pipelineDesc.PS = {g_ShapePS, sizeof(g_ShapePS)};
  pipelineDesc.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
  // This is interface, not scene: it is drawn in the order the caller asked for and neither tests
  // nor writes depth. Painter's order is the whole of its occlusion model, and blending is what
  // makes a 4%-white card fill mean what the design tokens say it means (ADR-014).
  pipelineDesc.BlendState = InterfaceBlendState();
  pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  winrt::check_hresult(_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_pipeline.put())));
}

void ShapeBackend::Draw(ID3D12GraphicsCommandList* _commandList, std::uint32_t _frameIndex, ShapeRenderer& _recorder)
{
  // A backend that was never Created has no pipeline, no root signature and no buffer the GPU can
  // read. This is fatal rather than a silent return, because a caller that reached here is a caller
  // that believes it is drawing to a screen.
  ASSERT_TEXT(m_pipeline != nullptr, L"Drawing through a ShapeBackend that was never created.");

  const ShapeRenderer::Batch batch = _recorder.TakeUnflushed();
  if (batch.vertices.empty())
  {
    return;
  }

  ASSERT_TEXT(batch.firstVertex + batch.vertices.size() <= ShapeRenderer::MAX_VERTICES_PER_FRAME,
              L"A shape batch runs past the end of its frame's slice.");

  // Into this frame's slice, at the offset the recorder says this batch occupies. Only the new
  // vertices are copied, and never over ones an earlier draw in this same command list has been
  // told to read.
  ShapeRenderer::ShapeVertex* slice = m_mappedVertices + static_cast<std::size_t>(_frameIndex) * ShapeRenderer::MAX_VERTICES_PER_FRAME;
  std::ranges::copy(batch.vertices, slice + batch.firstVertex);

  const std::uint64_t sliceOffsetBytes =
    static_cast<std::uint64_t>(_frameIndex) * ShapeRenderer::MAX_VERTICES_PER_FRAME * sizeof(ShapeRenderer::ShapeVertex);
  const auto pastThisBatch = static_cast<std::uint32_t>(batch.firstVertex + batch.vertices.size());

  D3D12_VERTEX_BUFFER_VIEW vertexView = {};
  vertexView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress() + sliceOffsetBytes;
  vertexView.SizeInBytes = pastThisBatch * sizeof(ShapeRenderer::ShapeVertex);
  vertexView.StrideInBytes = sizeof(ShapeRenderer::ShapeVertex);

  _commandList->SetGraphicsRootSignature(m_rootSignature.get());
  _commandList->SetPipelineState(m_pipeline.get());

  const std::array<float, CONSTANT_COUNT> canvasPixels = {static_cast<float>(SceneTarget::WIDTH_PIXELS),
                                                          static_cast<float>(SceneTarget::HEIGHT_PIXELS)};
  _commandList->SetGraphicsRoot32BitConstants(0, CONSTANT_COUNT, canvasPixels.data(), 0);

  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->IASetVertexBuffers(0, 1, &vertexView);
  _commandList->DrawInstanced(static_cast<std::uint32_t>(batch.vertices.size()), 1, batch.firstVertex, 0);
}

} // namespace Neuron
