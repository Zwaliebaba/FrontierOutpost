// PaletteTarget.cpp -- the 640x400 index buffer and the pass that resolves it through the
// palette. ADR-001 is the decision this file implements.

#include "pch.h"
#include "PaletteTarget.h"

#include "D3D12Defaults.h"

#include "CompiledShaders/PaletteResolveVS.h"
#include "CompiledShaders/PaletteResolvePS.h"

namespace Neuron
{

namespace
{

// One byte a texel, and the byte IS the palette index. R8_UINT rather than R8_UNORM on purpose:
// UNORM would make the shader read 1/255 and hand a filtering unit something to interpolate.
constexpr DXGI_FORMAT INDEX_TARGET_FORMAT = DXGI_FORMAT_R8_UINT;
constexpr DXGI_FORMAT DEPTH_TARGET_FORMAT = DXGI_FORMAT_D32_FLOAT;

// Sixteen 0x00RRGGBB entries packed as four uint4s, then the present scale. Root constants
// rather than a constant buffer: 17 DWORDs of the 64 the root signature has, no upload heap, no
// per-frame versioning, and nothing to keep alive.
constexpr std::uint32_t RESOLVE_CONSTANT_COUNT = PALETTE_SIZE + 1;

} // namespace

void PaletteTarget::Create(ID3D12Device* _device, DescriptorHeap& _shaderVisibleHeap, std::uint8_t _clearPaletteIndex)
{
  ASSERT_TEXT(_clearPaletteIndex < PALETTE_SIZE, L"There is no seventeenth color.");

  m_shaderVisibleHeap = &_shaderVisibleHeap;
  m_clearPaletteIndex = _clearPaletteIndex;

  CreateIndexTarget(_device, _clearPaletteIndex);
  CreateDepthTarget(_device);
  CreateResolvePipeline(_device);
}

void PaletteTarget::CreateIndexTarget(ID3D12Device* _device, std::uint8_t _clearPaletteIndex)
{
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC desc = Texture2DDesc(INDEX_TARGET_FORMAT, WIDTH_TEXELS, HEIGHT_TEXELS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

  // The optimized clear value has to equal what BeginScene actually clears with, or the debug
  // layer reports a mismatch -- which Device turns into a break, which is the point.
  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = INDEX_TARGET_FORMAT;
  clearValue.Color[0] = static_cast<float>(_clearPaletteIndex);
  clearValue.Color[1] = 0.0F;
  clearValue.Color[2] = 0.0F;
  clearValue.Color[3] = 0.0F;

  winrt::check_hresult(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue,
                                                        IID_PPV_ARGS(m_indexTarget.put())));

  D3D12_DESCRIPTOR_HEAP_DESC renderTargetHeapDesc = {};
  renderTargetHeapDesc.NumDescriptors = 1;
  renderTargetHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  winrt::check_hresult(_device->CreateDescriptorHeap(&renderTargetHeapDesc, IID_PPV_ARGS(m_renderTargetViewHeap.put())));
  _device->CreateRenderTargetView(m_indexTarget.get(), nullptr, m_renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart());

  m_indexTargetSlot = m_shaderVisibleHeap->Allocate();
  D3D12_SHADER_RESOURCE_VIEW_DESC shaderViewDesc = {};
  shaderViewDesc.Format = INDEX_TARGET_FORMAT;
  shaderViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  shaderViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  shaderViewDesc.Texture2D.MipLevels = 1;
  _device->CreateShaderResourceView(m_indexTarget.get(), &shaderViewDesc, m_shaderVisibleHeap->CpuHandle(m_indexTargetSlot));
}

void PaletteTarget::CreateDepthTarget(ID3D12Device* _device)
{
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC desc = Texture2DDesc(DEPTH_TARGET_FORMAT, WIDTH_TEXELS, HEIGHT_TEXELS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = DEPTH_TARGET_FORMAT;
  clearValue.DepthStencil.Depth = 1.0F;
  clearValue.DepthStencil.Stencil = 0;

  winrt::check_hresult(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                                                        IID_PPV_ARGS(m_depthTarget.put())));

  D3D12_DESCRIPTOR_HEAP_DESC depthHeapDesc = {};
  depthHeapDesc.NumDescriptors = 1;
  depthHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  winrt::check_hresult(_device->CreateDescriptorHeap(&depthHeapDesc, IID_PPV_ARGS(m_depthViewHeap.put())));
  _device->CreateDepthStencilView(m_depthTarget.get(), nullptr, m_depthViewHeap->GetCPUDescriptorHandleForHeapStart());
}

void PaletteTarget::CreateResolvePipeline(ID3D12Device* _device)
{
  D3D12_DESCRIPTOR_RANGE1 indexTargetRange = {};
  indexTargetRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  indexTargetRange.NumDescriptors = 1;
  indexTargetRange.BaseShaderRegister = 0;
  indexTargetRange.RegisterSpace = 0;
  // DATA_VOLATILE, and not either of the DATA_STATIC promises. This descriptor points at the
  // framebuffer: Resolve transitions it back to a render target in the same command list that
  // read it, which is exactly the change a DATA_STATIC range promises will not happen.
  indexTargetRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  indexTargetRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  std::array<D3D12_ROOT_PARAMETER1, 2> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &indexTargetRange;

  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.RegisterSpace = 0;
  parameters[1].Constants.Num32BitValues = RESOLVE_CONSTANT_COUNT;

  // The input assembler is opt-IN, so leaving ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT off is how a
  // root signature says it has no vertex layout. This pass does not: the fullscreen triangle
  // comes out of SV_VertexID.
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
    Fatal("Resolve root signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
  }
  winrt::check_hresult(serializeResult);
  winrt::check_hresult(_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                    IID_PPV_ARGS(m_resolveRootSignature.put())));

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = DefaultGraphicsPipeline();
  pipelineDesc.pRootSignature = m_resolveRootSignature.get();
  pipelineDesc.VS = {g_PaletteResolveVS, sizeof(g_PaletteResolveVS)};
  pipelineDesc.PS = {g_PaletteResolvePS, sizeof(g_PaletteResolvePS)};
  pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipelineDesc.SampleDesc.Count = 1;
  winrt::check_hresult(_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_resolvePipeline.put())));
}

D3D12_CPU_DESCRIPTOR_HANDLE PaletteTarget::DepthView() const noexcept
{
  return m_depthViewHeap->GetCPUDescriptorHandleForHeapStart();
}

void PaletteTarget::BeginScene(ID3D12GraphicsCommandList* _commandList)
{
  const D3D12_CPU_DESCRIPTOR_HANDLE indexView = m_renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
  const D3D12_CPU_DESCRIPTOR_HANDLE depthView = DepthView();
  _commandList->OMSetRenderTargets(1, &indexView, FALSE, &depthView);

  const D3D12_VIEWPORT viewport = {0.0F, 0.0F, static_cast<float>(WIDTH_TEXELS), static_cast<float>(HEIGHT_TEXELS), 0.0F, 1.0F};
  const D3D12_RECT scissor = {0, 0, static_cast<LONG>(WIDTH_TEXELS), static_cast<LONG>(HEIGHT_TEXELS)};
  _commandList->RSSetViewports(1, &viewport);
  _commandList->RSSetScissorRects(1, &scissor);

  // For an integer render target the runtime truncates these floats back to integers, so the
  // index arrives exactly. It has to match the optimized clear value baked into the resource.
  const float clearIndex[4] = {static_cast<float>(m_clearPaletteIndex), 0.0F, 0.0F, 0.0F};
  _commandList->ClearRenderTargetView(indexView, clearIndex, 0, nullptr);
  _commandList->ClearDepthStencilView(depthView, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
}

void PaletteTarget::Resolve(ID3D12GraphicsCommandList* _commandList, D3D12_CPU_DESCRIPTOR_HANDLE _backBufferView,
                            std::uint32_t _backBufferWidthPixels, std::uint32_t _backBufferHeightPixels, std::uint32_t _presentScale)
{
  DEBUG_ASSERT(_backBufferWidthPixels == WIDTH_TEXELS * _presentScale);
  DEBUG_ASSERT(_backBufferHeightPixels == HEIGHT_TEXELS * _presentScale);

  const D3D12_RESOURCE_BARRIER toShaderResource =
    TransitionBarrier(m_indexTarget.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  _commandList->ResourceBarrier(1, &toShaderResource);

  _commandList->OMSetRenderTargets(1, &_backBufferView, FALSE, nullptr);

  const D3D12_VIEWPORT viewport = {0.0F, 0.0F, static_cast<float>(_backBufferWidthPixels), static_cast<float>(_backBufferHeightPixels),
                                   0.0F, 1.0F};
  const D3D12_RECT scissor = {0, 0, static_cast<LONG>(_backBufferWidthPixels), static_cast<LONG>(_backBufferHeightPixels)};
  _commandList->RSSetViewports(1, &viewport);
  _commandList->RSSetScissorRects(1, &scissor);

  ID3D12DescriptorHeap* heaps[] = {m_shaderVisibleHeap->Handle()};
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetGraphicsRootSignature(m_resolveRootSignature.get());
  _commandList->SetPipelineState(m_resolvePipeline.get());
  _commandList->SetGraphicsRootDescriptorTable(0, m_shaderVisibleHeap->GpuHandle(m_indexTargetSlot));
  _commandList->SetGraphicsRoot32BitConstants(1, PALETTE_SIZE, EGA_PALETTE.data(), 0);
  _commandList->SetGraphicsRoot32BitConstants(1, 1, &_presentScale, PALETTE_SIZE);

  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->DrawInstanced(3, 1, 0, 0);

  const D3D12_RESOURCE_BARRIER backToRenderTarget =
    TransitionBarrier(m_indexTarget.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  _commandList->ResourceBarrier(1, &backToRenderTarget);
}

} // namespace Neuron
