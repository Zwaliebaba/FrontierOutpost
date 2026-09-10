// MeshRenderer.cpp -- the mesh pass. ADR-012 (two-tone flat shading) and ADR-003 (the camera).

#include "pch.h"
#include "MeshRenderer.h"

#include "D3D12Defaults.h"

#include "CompiledShaders/MeshVS.h"
#include "CompiledShaders/MeshPS.h"

namespace Neuron
{

void MeshRenderer::Create(ID3D12Device* _device)
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

  // R8G8B8A8_UNORM on the two colors, not R32_UINT: the four bytes Pack() wrote arrive in the
  // shader as a float4 already divided by 255, so neither side does any unpacking arithmetic and
  // the channel order is stated once, in Color.h, rather than in both places.
  const std::array<D3D12_INPUT_ELEMENT_DESC, 4> inputLayout = {
    D3D12_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, positionX),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, normalX),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(MeshVertex, shadedColor),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(MeshVertex, litColor),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = DefaultGraphicsPipeline();
  pipelineDesc.pRootSignature = m_rootSignature.get();
  pipelineDesc.VS = {g_MeshVS, sizeof(g_MeshVS)};
  pipelineDesc.PS = {g_MeshPS, sizeof(g_MeshPS)};
  pipelineDesc.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
  // Depth on, and no culling. The depth buffer is what resolves a hull against itself and the
  // ship against the station; culling would additionally require every mesh to be a closed solid
  // with consistent winding, which the ship's wings and the station's panels deliberately are not
  // -- they are flat and are seen from above from every angle the camera can be at.
  pipelineDesc.DepthStencilState = DepthState(true);
  pipelineDesc.RasterizerState = SolidRasterizer(D3D12_CULL_MODE_NONE);
  pipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  winrt::check_hresult(_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_pipeline.put())));
}

void MeshRenderer::Draw(ID3D12GraphicsCommandList* _commandList, const Mesh& _mesh, const IsometricCamera& _camera,
                        const std::array<float, 16>& _worldMatrix)
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

  const D3D12_VERTEX_BUFFER_VIEW vertexView = _mesh.VertexView();
  const D3D12_INDEX_BUFFER_VIEW indexView = _mesh.IndexView();
  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->IASetVertexBuffers(0, 1, &vertexView);
  _commandList->IASetIndexBuffer(&indexView);
  _commandList->DrawIndexedInstanced(_mesh.IndexCount(), 1, 0, 0, 0);
}

} // namespace Neuron
