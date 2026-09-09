// MeshRenderer.cpp -- the mesh pass. ADR-002 (two-tone flat shading) and ADR-003 (the camera).

#include "pch.h"
#include "MeshRenderer.h"

#include "D3D12Defaults.h"
#include "PaletteTarget.h"

#include "CompiledShaders/MeshVS.h"
#include "CompiledShaders/MeshPS.h"

namespace Neuron
{

void MeshRenderer::Create(ID3D12Device* _device, std::span<const MeshVertex> _vertices, std::span<const std::uint16_t> _indices)
{
  CreatePipeline(_device);
  CreateBuffers(_device, _vertices, _indices);
}

void MeshRenderer::CreatePipeline(ID3D12Device* _device)
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
    Fatal("Mesh root signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
  }
  winrt::check_hresult(serializeResult);
  winrt::check_hresult(
    _device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  const std::array<D3D12_INPUT_ELEMENT_DESC, 3> inputLayout = {
    D3D12_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, positionX),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, normalX),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"TEXCOORD", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(MeshVertex, paletteIndex),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = DefaultGraphicsPipeline();
  pipelineDesc.pRootSignature = m_rootSignature.get();
  pipelineDesc.VS = {g_MeshVS, sizeof(g_MeshVS)};
  pipelineDesc.PS = {g_MeshPS, sizeof(g_MeshPS)};
  pipelineDesc.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
  // Depth on, and no culling. The depth buffer is what resolves the hull against itself; culling
  // would additionally require every mesh to be a closed solid with consistent winding, which the
  // wings deliberately are not -- they are flat and are seen from above from every angle the
  // camera can be at.
  pipelineDesc.DepthStencilState = DepthState(true);
  pipelineDesc.RasterizerState = SolidRasterizer(D3D12_CULL_MODE_NONE);
  pipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8_UINT;
  winrt::check_hresult(_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_pipeline.put())));
}

void MeshRenderer::CreateBuffers(ID3D12Device* _device, std::span<const MeshVertex> _vertices, std::span<const std::uint16_t> _indices)
{
  ASSERT_TEXT(!_vertices.empty() && !_indices.empty(), L"A mesh with no triangles is an authoring mistake, not a valid mesh.");

  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);

  // An upload heap for a mesh of a few dozen triangles. A default-heap copy would be the right
  // answer for real scenery; for 1.7 KB read once a frame it is machinery with nothing to buy.
  const auto vertexBytes = static_cast<std::uint64_t>(_vertices.size_bytes());
  const D3D12_RESOURCE_DESC vertexDesc = BufferDesc(vertexBytes);
  winrt::check_hresult(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(m_vertices.put())));

  const auto indexBytes = static_cast<std::uint64_t>(_indices.size_bytes());
  const D3D12_RESOURCE_DESC indexDesc = BufferDesc(indexBytes);
  winrt::check_hresult(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &indexDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(m_indices.put())));

  const D3D12_RANGE readNothing = {0, 0};
  void* mapped = nullptr;

  winrt::check_hresult(m_vertices->Map(0, &readNothing, &mapped));
  std::memcpy(mapped, _vertices.data(), _vertices.size_bytes());
  m_vertices->Unmap(0, nullptr);

  winrt::check_hresult(m_indices->Map(0, &readNothing, &mapped));
  std::memcpy(mapped, _indices.data(), _indices.size_bytes());
  m_indices->Unmap(0, nullptr);

  m_vertexView.BufferLocation = m_vertices->GetGPUVirtualAddress();
  m_vertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
  m_vertexView.StrideInBytes = sizeof(MeshVertex);

  m_indexView.BufferLocation = m_indices->GetGPUVirtualAddress();
  m_indexView.SizeInBytes = static_cast<UINT>(indexBytes);
  m_indexView.Format = DXGI_FORMAT_R16_UINT;

  m_indexCount = static_cast<std::uint32_t>(_indices.size());
}

void MeshRenderer::Draw(ID3D12GraphicsCommandList* _commandList, const IsometricCamera& _camera, const std::array<float, 16>& _worldMatrix)
{
  std::array<float, CONSTANT_COUNT> constants = {};
  const std::array<float, 16> viewProjection = _camera.ViewProjection();

  std::copy(viewProjection.begin(), viewProjection.end(), constants.begin());
  std::copy(_worldMatrix.begin(), _worldMatrix.end(), constants.begin() + 16);
  constants[32] = LIGHT_DIRECTION_X;
  constants[33] = LIGHT_DIRECTION_Y;
  constants[34] = LIGHT_DIRECTION_Z;
  constants[35] = LIT_THRESHOLD;

  _commandList->SetGraphicsRootSignature(m_rootSignature.get());
  _commandList->SetPipelineState(m_pipeline.get());
  _commandList->SetGraphicsRoot32BitConstants(0, CONSTANT_COUNT, constants.data(), 0);

  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->IASetVertexBuffers(0, 1, &m_vertexView);
  _commandList->IASetIndexBuffer(&m_indexView);
  _commandList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
}

} // namespace Neuron
