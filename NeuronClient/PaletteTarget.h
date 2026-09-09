#pragma once

#include "DescriptorHeap.h"
#include "Palette.h"

namespace Neuron
{

/// The 640x400 paletted framebuffer the game draws into, and the pass that puts it on the screen.
///
/// Every pass in this renderer writes a palette INDEX into an R8_UINT target -- never a color.
/// One resolve pass at the end reads the index, looks it up in the 16-entry palette and writes
/// the back buffer. The alternative, rendering in RGBA and quantizing at the end, is rejected in
/// ADR-001: it puts the palette decision after the anti-aliasing rather than before it, and the
/// crisp edges the legacy look depends on are exactly what it loses.
class PaletteTarget
{
public:
  /// The virtual screen. Fixed, not configurable: Design/README.md section 1 makes it a design
  /// constraint rather than a setting.
  static constexpr std::uint32_t WIDTH_TEXELS = 640;
  static constexpr std::uint32_t HEIGHT_TEXELS = 400;

  /// _clearPaletteIndex is the color of empty space, and it is fixed at creation because D3D12
  /// wants the optimized clear value baked into the resource. Clearing to anything else would be
  /// a debug-layer warning, which Device turns into a break.
  void Create(ID3D12Device* _device, DescriptorHeap& _shaderVisibleHeap, std::uint8_t _clearPaletteIndex);

  /// Binds the index target and its depth buffer, sets the 640x400 viewport, and clears both.
  /// Everything the game draws happens between this and Resolve().
  void BeginScene(ID3D12GraphicsCommandList* _commandList);

  /// Reads the index target and writes the back buffer through the palette. _presentScale is the
  /// whole-number blow-up factor; the shader divides by it with an integer divide, so there is no
  /// sampler anywhere on this path and no way to introduce filtering by accident.
  void Resolve(ID3D12GraphicsCommandList* _commandList, D3D12_CPU_DESCRIPTOR_HANDLE _backBufferView, std::uint32_t _backBufferWidthPixels,
               std::uint32_t _backBufferHeightPixels, std::uint32_t _presentScale);

  /// The depth buffer the mesh pass shares with the index target. Same 640x400 footprint, so a
  /// pixel's depth and its color index are decided at the same resolution.
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE DepthView() const noexcept;

private:
  void CreateIndexTarget(ID3D12Device* _device, std::uint8_t _clearPaletteIndex);
  void CreateDepthTarget(ID3D12Device* _device);
  void CreateResolvePipeline(ID3D12Device* _device);

  winrt::com_ptr<ID3D12Resource> m_indexTarget;
  winrt::com_ptr<ID3D12Resource> m_depthTarget;
  winrt::com_ptr<ID3D12DescriptorHeap> m_renderTargetViewHeap;
  winrt::com_ptr<ID3D12DescriptorHeap> m_depthViewHeap;
  winrt::com_ptr<ID3D12RootSignature> m_resolveRootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_resolvePipeline;

  DescriptorHeap* m_shaderVisibleHeap = nullptr;
  std::uint32_t m_indexTargetSlot = 0;
  std::uint8_t m_clearPaletteIndex = 0;
};

} // namespace Neuron
