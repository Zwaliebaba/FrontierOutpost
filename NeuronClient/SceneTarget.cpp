// SceneTarget.cpp -- the 1280x720 canvas, its depth buffer, and the pass that presents it at a
// whole-number scale (ADR-075).

#include "pch.h"
#include "SceneTarget.h"

#include "CompiledShaders/CanvasPS.h"
#include "CompiledShaders/CanvasVS.h"
#include "D3D12Defaults.h"
#include "DescriptorHeap.h"

namespace Neuron
{

namespace
{

constexpr DXGI_FORMAT CANVAS_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT DEPTH_TARGET_FORMAT = DXGI_FORMAT_D32_FLOAT;

/// offsetXPixels, offsetYPixels, scale -- what CanvasPS.hlsl's cbuffer declares, in that order.
constexpr std::uint32_t CANVAS_CONSTANT_COUNT = 3;

/// The four channels of a Color as the 0-1 floats ClearRenderTargetView wants.
///
/// UNORM, so a channel is the byte divided by 255. The canvas is deliberately not _SRGB (ADR-011),
/// so these floats land in memory as the bytes they came from: a screen cleared to BLUE reads
/// 0000AA exactly.
[[nodiscard]] std::array<float, 4> ClearValue(const Color& _color) noexcept
{
  return {
    static_cast<float>(_color.red) / 255.0F,
    static_cast<float>(_color.green) / 255.0F,
    static_cast<float>(_color.blue) / 255.0F,
    static_cast<float>(_color.alpha) / 255.0F,
  };
}

} // namespace

void SceneTarget::Create(ID3D12Device* _device, DescriptorHeap& _shaderVisibleHeap, const Color& _clearColor)
{
  m_clearColor = _clearColor;
  m_shaderVisibleHeap = &_shaderVisibleHeap;

  CreateCanvas(_device, _shaderVisibleHeap);
  CreateDepthTarget(_device);
  CreateResolvePipeline(_device);
}

void SceneTarget::CreateCanvas(ID3D12Device* _device, DescriptorHeap& _shaderVisibleHeap)
{
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC desc = Texture2DDesc(CANVAS_FORMAT, WIDTH_PIXELS, HEIGHT_PIXELS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

  const std::array<float, 4> clear = ClearValue(m_clearColor);
  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = CANVAS_FORMAT;
  std::ranges::copy(clear, static_cast<float*>(clearValue.Color));

  // RENDER_TARGET is where a frame both starts and ends: BeginScene draws into it and Present
  // returns it to this state after reading it, so the state at Create is the state every frame
  // assumes.
  winrt::check_hresult(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue,
                                                        IID_PPV_ARGS(m_canvas.put())));

  D3D12_DESCRIPTOR_HEAP_DESC canvasHeapDesc = {};
  canvasHeapDesc.NumDescriptors = 1;
  canvasHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  winrt::check_hresult(_device->CreateDescriptorHeap(&canvasHeapDesc, IID_PPV_ARGS(m_canvasViewHeap.put())));
  _device->CreateRenderTargetView(m_canvas.get(), nullptr, CanvasView());

  m_canvasSlot = _shaderVisibleHeap.Allocate();
  D3D12_SHADER_RESOURCE_VIEW_DESC canvasView = {};
  canvasView.Format = CANVAS_FORMAT;
  canvasView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  canvasView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  canvasView.Texture2D.MipLevels = 1;
  _device->CreateShaderResourceView(m_canvas.get(), &canvasView, _shaderVisibleHeap.CpuHandle(m_canvasSlot));
}

void SceneTarget::CreateDepthTarget(ID3D12Device* _device)
{
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC desc = Texture2DDesc(DEPTH_TARGET_FORMAT, WIDTH_PIXELS, HEIGHT_PIXELS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

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

void SceneTarget::CreateResolvePipeline(ID3D12Device* _device)
{
  D3D12_DESCRIPTOR_RANGE1 canvasRange = {};
  canvasRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  canvasRange.NumDescriptors = 1;
  canvasRange.BaseShaderRegister = 0;
  canvasRange.RegisterSpace = 0;
  // NOT DATA_STATIC, which is what the font atlas gets. The canvas is written by every pass of
  // every frame and read by this one, so the descriptor's data changes constantly; promising the
  // driver otherwise is a promise this resource cannot keep.
  canvasRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  canvasRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  std::array<D3D12_ROOT_PARAMETER1, 2> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &canvasRange;

  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.RegisterSpace = 0;
  parameters[1].Constants.Num32BitValues = CANVAS_CONSTANT_COUNT;

  // No ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, unlike the other two pipelines: CanvasVS builds its
  // three vertices out of SV_VertexID and there is no vertex buffer for an input assembler to
  // read.
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
    Fatal("Canvas root signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
  }
  winrt::check_hresult(serializeResult);
  winrt::check_hresult(_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                    IID_PPV_ARGS(m_resolveRootSignature.put())));

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = DefaultGraphicsPipeline();
  pipelineDesc.pRootSignature = m_resolveRootSignature.get();
  pipelineDesc.VS = {g_CanvasVS, sizeof(g_CanvasVS)};
  pipelineDesc.PS = {g_CanvasPS, sizeof(g_CanvasPS)};
  // The shared defaults are exactly right here and none of them is overridden: opaque, no
  // multisampling, no depth, no input layout. This pass replaces every pixel it touches with the
  // canvas's, and there is nothing underneath it to blend against.
  pipelineDesc.RTVFormats[0] = CANVAS_FORMAT;
  winrt::check_hresult(_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_resolvePipeline.put())));
}

D3D12_CPU_DESCRIPTOR_HANDLE SceneTarget::CanvasView() const noexcept
{
  return m_canvasViewHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE SceneTarget::DepthView() const noexcept
{
  return m_depthViewHeap->GetCPUDescriptorHandleForHeapStart();
}

void SceneTarget::BeginScene(ID3D12GraphicsCommandList* _commandList)
{
  const D3D12_CPU_DESCRIPTOR_HANDLE canvasView = CanvasView();
  const D3D12_CPU_DESCRIPTOR_HANDLE depthView = DepthView();
  _commandList->OMSetRenderTargets(1, &canvasView, FALSE, &depthView);

  const D3D12_VIEWPORT viewport = {0.0F, 0.0F, static_cast<float>(WIDTH_PIXELS), static_cast<float>(HEIGHT_PIXELS), 0.0F, 1.0F};
  const D3D12_RECT scissor = {0, 0, static_cast<LONG>(WIDTH_PIXELS), static_cast<LONG>(HEIGHT_PIXELS)};
  _commandList->RSSetViewports(1, &viewport);
  _commandList->RSSetScissorRects(1, &scissor);

  const std::array<float, 4> clearColor = ClearValue(m_clearColor);
  _commandList->ClearRenderTargetView(canvasView, clearColor.data(), 0, nullptr);
  _commandList->ClearDepthStencilView(depthView, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
}

void SceneTarget::Present(ID3D12GraphicsCommandList* _commandList, D3D12_CPU_DESCRIPTOR_HANDLE _backBufferView,
                          const Presentation& _presentation)
{
  const D3D12_RESOURCE_BARRIER toShaderResource =
    TransitionBarrier(m_canvas.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  _commandList->ResourceBarrier(1, &toShaderResource);

  // No depth. The canvas is already flat and this pass writes every pixel of its own rectangle.
  _commandList->OMSetRenderTargets(1, &_backBufferView, FALSE, nullptr);

  const D3D12_VIEWPORT viewport = {
    0.0F, 0.0F, static_cast<float>(_presentation.surfaceWidthPixels), static_cast<float>(_presentation.surfaceHeightPixels), 0.0F, 1.0F};
  _commandList->RSSetViewports(1, &viewport);

  // Black is the letterbox, and it is cleared BEFORE the scissor narrows to the canvas: the clear
  // takes its own rectangle list rather than obeying the scissor, and the point of it is to paint
  // the part of the back buffer the draw below will not reach. At scale 1 in a window that is
  // exactly the canvas there is no such part, and this writes 1280x720 pixels that are about to be
  // overwritten -- which is one clear on a surface the size of the screen, and the price of not
  // having two code paths.
  const std::array<float, 4> letterbox = ClearValue(BLACK);
  _commandList->ClearRenderTargetView(_backBufferView, letterbox.data(), 0, nullptr);

  // The canvas rectangle, and it is what keeps CanvasPS's subtraction from going negative in the
  // letterbox. See the shader: this is not an optimization.
  const D3D12_RECT scissor = {
    static_cast<LONG>(_presentation.offsetXPixels),
    static_cast<LONG>(_presentation.offsetYPixels),
    static_cast<LONG>(_presentation.offsetXPixels + WIDTH_PIXELS * _presentation.scale),
    static_cast<LONG>(_presentation.offsetYPixels + HEIGHT_PIXELS * _presentation.scale),
  };
  _commandList->RSSetScissorRects(1, &scissor);

  ID3D12DescriptorHeap* heaps[] = {m_shaderVisibleHeap->Handle()};
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetGraphicsRootSignature(m_resolveRootSignature.get());
  _commandList->SetPipelineState(m_resolvePipeline.get());
  _commandList->SetGraphicsRootDescriptorTable(0, m_shaderVisibleHeap->GpuHandle(m_canvasSlot));

  const std::array<std::uint32_t, CANVAS_CONSTANT_COUNT> constants = {
    _presentation.offsetXPixels,
    _presentation.offsetYPixels,
    _presentation.scale,
  };
  _commandList->SetGraphicsRoot32BitConstants(1, CANVAS_CONSTANT_COUNT, constants.data(), 0);

  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->DrawInstanced(3, 1, 0, 0);

  const D3D12_RESOURCE_BARRIER toRenderTarget =
    TransitionBarrier(m_canvas.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  _commandList->ResourceBarrier(1, &toRenderTarget);
}

} // namespace Neuron
