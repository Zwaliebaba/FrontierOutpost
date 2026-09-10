// Starfield.cpp -- the backdrop pass. ADR-010 is the decision; StarfieldPS.hlsl is the hash.

#include "pch.h"
#include "Starfield.h"

#include "D3D12Defaults.h"
#include "PaletteTarget.h"

#include "CompiledShaders/StarfieldVS.h"
#include "CompiledShaders/StarfieldPS.h"

namespace Neuron
{

namespace
{

/// Division that rounds towards negative infinity rather than towards zero.
///
/// C++ truncates, which puts a seam at the origin: -1 / 8 and 1 / 8 are both 0, so a layer would
/// hold still across two pixels of camera movement and then jump. It is the same argument that
/// made Follow() use std::round rather than a cast, one level further out.
[[nodiscard]] std::int32_t FloorDivide(std::int64_t _value, std::int32_t _divisor) noexcept
{
  const std::int64_t quotient = _value / _divisor;
  const bool exact = (_value % _divisor) == 0;
  const bool negative = _value < 0;
  return static_cast<std::int32_t>((negative && !exact) ? quotient - 1 : quotient);
}

} // namespace

std::int32_t Starfield::LayerOffset(float _snappedCameraPixels, std::size_t _layer)
{
  DEBUG_ASSERT(_layer < LAYER_COUNT);

  // The camera's offset is already a whole number of pixels; llround only turns the float that
  // holds it back into an integer.
  const auto pixels = static_cast<std::int64_t>(std::llround(_snappedCameraPixels));
  return FloorDivide(pixels, LAYER_PARALLAX_DIVISORS[_layer]);
}

void Starfield::Create(ID3D12Device* _device)
{
  std::array<D3D12_ROOT_PARAMETER1, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[0].Constants.ShaderRegister = 0;
  parameters[0].Constants.RegisterSpace = 0;
  parameters[0].Constants.Num32BitValues = CONSTANT_COUNT;

  // No input layout: the fullscreen triangle comes out of SV_VertexID.
  const D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {
    .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
    .Desc_1_1 = {.NumParameters = static_cast<UINT>(parameters.size()),
                 .pParameters = parameters.data(),
                 .NumStaticSamplers = 0,
                 .pStaticSamplers = nullptr,
                 .Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE},
  };

  winrt::com_ptr<ID3DBlob> serialized;
  winrt::com_ptr<ID3DBlob> errors;
  const HRESULT serializeResult = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, serialized.put(), errors.put());
  if (FAILED(serializeResult) && errors)
  {
    Fatal("Starfield root signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
  }
  winrt::check_hresult(serializeResult);
  winrt::check_hresult(
    _device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = DefaultGraphicsPipeline();
  pipelineDesc.pRootSignature = m_rootSignature.get();
  pipelineDesc.VS = {g_StarfieldVS, sizeof(g_StarfieldVS)};
  pipelineDesc.PS = {g_StarfieldPS, sizeof(g_StarfieldPS)};
  // The backdrop is behind everything by being drawn before it, not by depth. It neither tests
  // nor writes depth, so the depth buffer the mesh pass is about to use is left exactly as
  // BeginScene cleared it.
  pipelineDesc.DepthStencilState = DepthState(false);
  pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8_UINT;
  winrt::check_hresult(_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_pipeline.put())));
}

void Starfield::Draw(ID3D12GraphicsCommandList* _commandList, const IsometricCamera& _camera)
{
  // Four values a layer: the offset in xy, and two of padding the shader ignores.
  std::array<std::int32_t, CONSTANT_COUNT> constants = {};
  for (std::size_t layer = 0; layer < LAYER_COUNT; ++layer)
  {
    constants[layer * 4 + 0] = LayerOffset(_camera.SnappedTargetXPixels(), layer);
    constants[layer * 4 + 1] = LayerOffset(_camera.SnappedTargetYPixels(), layer);
  }

  _commandList->SetGraphicsRootSignature(m_rootSignature.get());
  _commandList->SetPipelineState(m_pipeline.get());
  _commandList->SetGraphicsRoot32BitConstants(0, CONSTANT_COUNT, constants.data(), 0);

  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->DrawInstanced(3, 1, 0, 0);
}

} // namespace Neuron
