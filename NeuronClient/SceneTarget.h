#pragma once

#include "Color.h"

namespace Neuron
{

/// The 1280x720 screen the game draws into, and the depth buffer that goes with it.
///
/// Every pass in this renderer writes an R8G8B8A8_UNORM color straight into the swap chain's back
/// buffer. There is no intermediate surface and no resolve pass: at a present scale of one the
/// render target, the back buffer and the client area are the same 1280x720 pixels, so a pass that
/// copied one to the other would be copying a picture onto itself (ADR-011).
///
/// Until 2026-09-10 this type was `PaletteTarget` and the picture was a 640x400 R8_UINT buffer of
/// palette indices, blown up 2x and looked up through a 16-entry table by a fullscreen pass
/// (ADR-001). What is left of it is this class's remaining job -- owning the depth buffer, and
/// being the one place that says what a frame's opening state is.
class SceneTarget
{
public:
  /// The screen. Fixed, not configurable: Design/README.md section 1 makes it a design constraint
  /// rather than a setting, and the window is created at exactly this size.
  static constexpr std::uint32_t WIDTH_PIXELS = 1280;
  static constexpr std::uint32_t HEIGHT_PIXELS = 720;

  /// _clearColor is the color of empty space. Unlike the index target it replaces, this is not
  /// baked into a resource: the back buffer is the swap chain's and carries no optimized clear
  /// value, so the clear color is free to be anything and is kept here only so that BeginScene
  /// needs no argument for it.
  void Create(ID3D12Device* _device, const Color& _clearColor);

  /// Binds the back buffer and the depth buffer, sets the 1280x720 viewport, and clears both.
  /// Everything the game draws happens after this and before the frame is presented.
  void BeginScene(ID3D12GraphicsCommandList* _commandList, D3D12_CPU_DESCRIPTOR_HANDLE _backBufferView);

  /// The depth buffer the mesh pass uses. Same 1280x720 footprint as the back buffer, so a pixel's
  /// depth and its color are decided at the same resolution.
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE DepthView() const noexcept;

private:
  void CreateDepthTarget(ID3D12Device* _device);

  winrt::com_ptr<ID3D12Resource> m_depthTarget;
  winrt::com_ptr<ID3D12DescriptorHeap> m_depthViewHeap;

  Color m_clearColor = BLACK;
};

} // namespace Neuron
