// SceneTarget.cpp -- the depth buffer for the 1280x720 screen, and what a frame opens with (ADR-011).

#include "pch.h"
#include "SceneTarget.h"

#include "D3D12Defaults.h"

namespace Neuron
{

namespace
{

constexpr DXGI_FORMAT DEPTH_TARGET_FORMAT = DXGI_FORMAT_D32_FLOAT;

} // namespace

void SceneTarget::Create(ID3D12Device* _device, const Color& _clearColor)
{
  m_clearColor = _clearColor;

  CreateDepthTarget(_device);
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

D3D12_CPU_DESCRIPTOR_HANDLE SceneTarget::DepthView() const noexcept
{
  return m_depthViewHeap->GetCPUDescriptorHandleForHeapStart();
}

void SceneTarget::BeginScene(ID3D12GraphicsCommandList* _commandList, D3D12_CPU_DESCRIPTOR_HANDLE _backBufferView)
{
  const D3D12_CPU_DESCRIPTOR_HANDLE depthView = DepthView();
  _commandList->OMSetRenderTargets(1, &_backBufferView, FALSE, &depthView);

  const D3D12_VIEWPORT viewport = {0.0F, 0.0F, static_cast<float>(WIDTH_PIXELS), static_cast<float>(HEIGHT_PIXELS), 0.0F, 1.0F};
  const D3D12_RECT scissor = {0, 0, static_cast<LONG>(WIDTH_PIXELS), static_cast<LONG>(HEIGHT_PIXELS)};
  _commandList->RSSetViewports(1, &viewport);
  _commandList->RSSetScissorRects(1, &scissor);

  // UNORM, so the four channels are the byte divided by 255. The back buffer is deliberately not
  // _SRGB (ADR-011), so these floats land in the swap chain as the bytes they came from: a
  // screenshot of a screen cleared to BLUE reads 0000AA exactly.
  const float clearColor[4] = {
    static_cast<float>(m_clearColor.red) / 255.0F,
    static_cast<float>(m_clearColor.green) / 255.0F,
    static_cast<float>(m_clearColor.blue) / 255.0F,
    static_cast<float>(m_clearColor.alpha) / 255.0F,
  };
  _commandList->ClearRenderTargetView(_backBufferView, clearColor, 0, nullptr);
  _commandList->ClearDepthStencilView(depthView, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
}

} // namespace Neuron
