#pragma once

#include "Color.h"
#include "Presentation.h"

namespace Neuron
{

class DescriptorHeap;

/// The 1280x720 canvas the game draws into, the depth buffer that goes with it, and the one pass
/// that puts the canvas on the display.
///
/// Every pass in this renderer writes an R8G8B8A8_UNORM color into the CANVAS, never into the swap
/// chain's back buffer. Present() then copies the canvas into the back buffer at a whole-number
/// scale, centered, with the remainder cleared to black (ADR-075). Nothing on that path resamples
/// anything: the copy reads the canvas with an integer texel load and divides the pixel position
/// by an integer.
///
/// This class is therefore the one place that says what a frame opens with AND what it closes
/// with. The two belong together because they are the same statement made twice -- the canvas is
/// the render target BeginScene binds and the shader resource Present reads, and splitting them
/// would put its two resource states in two files.
class SceneTarget
{
public:
  /// The canvas, defined from Presentation.h rather than stated again. Presentation is the header
  /// a second platform reuses unchanged, so it cannot take its size from a class that owns D3D12
  /// resources -- the number lives there and this is the name the rest of the tree spells it by.
  static constexpr std::uint32_t WIDTH_PIXELS = Presentation::CANVAS_WIDTH_PIXELS;
  static constexpr std::uint32_t HEIGHT_PIXELS = Presentation::CANVAS_HEIGHT_PIXELS;

  /// _clearColor is the color of empty space. The canvas is this class's own resource, so the
  /// color is baked into its optimized clear value as well as kept here; a clear to any other
  /// color still works and is merely slower, which is the trade an optimized value makes.
  ///
  /// _shaderVisibleHeap is where the canvas's SRV goes. One slot, allocated once, never returned:
  /// the canvas outlives every frame (DescriptorHeap.h).
  void Create(ID3D12Device* _device, DescriptorHeap& _shaderVisibleHeap, const Color& _clearColor);

  /// Binds the canvas and the depth buffer, sets the 1280x720 viewport, and clears both.
  /// Everything the game draws happens after this and before Present().
  ///
  /// It takes no back-buffer view any more. The game does not know the back buffer exists.
  void BeginScene(ID3D12GraphicsCommandList* _commandList);

  /// Copies the finished canvas into the back buffer at _presentation's scale and offset, and
  /// clears everything around it to black.
  ///
  /// The back buffer is already in RENDER_TARGET here -- Device::BeginFrame put it there and
  /// Device::EndFrameAndPresent takes it back to PRESENT -- so the only barriers this needs are
  /// the canvas's own, out to PIXEL_SHADER_RESOURCE and back.
  void Present(ID3D12GraphicsCommandList* _commandList, D3D12_CPU_DESCRIPTOR_HANDLE _backBufferView, const Presentation& _presentation);

  /// The depth buffer the mesh pass uses. Same 1280x720 footprint as the canvas, so a pixel's
  /// depth and its color are decided at the same resolution.
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE DepthView() const noexcept;

private:
  void CreateCanvas(ID3D12Device* _device, DescriptorHeap& _shaderVisibleHeap);
  void CreateDepthTarget(ID3D12Device* _device);
  void CreateResolvePipeline(ID3D12Device* _device);

  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CanvasView() const noexcept;

  winrt::com_ptr<ID3D12Resource> m_canvas;
  winrt::com_ptr<ID3D12DescriptorHeap> m_canvasViewHeap;
  winrt::com_ptr<ID3D12Resource> m_depthTarget;
  winrt::com_ptr<ID3D12DescriptorHeap> m_depthViewHeap;

  winrt::com_ptr<ID3D12RootSignature> m_resolveRootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_resolvePipeline;

  /// Not owned. The one shader-visible heap the client has, kept so that Present can bind it and
  /// reach the canvas's slot.
  DescriptorHeap* m_shaderVisibleHeap = nullptr;
  std::uint32_t m_canvasSlot = 0;

  Color m_clearColor = BLACK;
};

} // namespace Neuron
