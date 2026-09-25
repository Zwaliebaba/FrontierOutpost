// NeuronClient/SwapChain.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Neuron
{

struct GraphicsCore;
class Texture;
class Window;

/// Two flip-discard buffers on a window, which a frame reaches through the present pass
/// (Design/ADR/ADR-007; plan §5.3). The frame renders offscreen, and Present draws it into the back
/// buffer one pixel to one pixel, flipped once: the image keeps GL's layout, with row 0 at the
/// bottom, and the window shows it the right way up (plan §5.5). There is no exclusive fullscreen,
/// and Alt+Enter does nothing. A device makes it, GraphicsDevice::CreateSwapChain. It is not
/// thread-safe: the device's thread uses it.
class SwapChain
{
public:
  struct Desc
  {
    const Window* window;      // which outlives the swap chain
    std::uint32_t widthPixels; // of the window's client area
    std::uint32_t heightPixels;
    bool vsync; // off, each frame is shown once it is ready, torn where the display allows (N7)
  };

  SwapChain() noexcept;
  /// Waits for the GPU to finish with the buffers, and lets them go at once, so that the swap chain
  /// goes before its window.
  ~SwapChain();
  SwapChain(SwapChain&& _other) noexcept;
  SwapChain& operator=(SwapChain&& _other) noexcept;
  SwapChain(const SwapChain&) = delete;
  SwapChain& operator=(const SwapChain&) = delete;

  /// False for a swap chain that was never made, or that the device failed to make.
  explicit operator bool() const noexcept;

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept;
  [[nodiscard]] std::uint32_t HeightPixels() const noexcept;

  /// Whether a frame shown with vsync off may tear, as the display and Windows allow.
  [[nodiscard]] bool IsTearingAllowed() const noexcept;

  /// Makes the buffers _width by _height, as the client area now is, after waiting for the GPU to
  /// finish with them. A size of 0, as a minimized window has, is ignored.
  void Resize(std::uint32_t _widthPixels, std::uint32_t _heightPixels);

  /// Submits what the context recorded, then shows _image, a 2D colour texture: the present pass
  /// draws it into the back buffer, flipped once. An image of another size than the buffers is
  /// drawn from its top-left corner, cut off or with black past it. Call it once a frame, before
  /// GraphicsDevice::EndFrame.
  void Present(const Texture& _image);

  /// As Present, and gives what it showed: RGBA8, top row first, as ImageFile encodes it. Waits for
  /// the GPU. Returns false, and leaves _outTexels alone, when nothing was shown.
  [[nodiscard]] bool PresentAndCapture(const Texture& _image, std::vector<std::byte>& _outTexels);

private:
  friend class GraphicsDevice;
  struct Native;

  /// Makes the swap chain, or reports why not and returns an empty one.
  [[nodiscard]] static SwapChain Make(const std::shared_ptr<GraphicsCore>& _core, const Desc& _desc);

  /// Shows _image, and reads it back into _outCapture when that is not null.
  bool Show(const Texture& _image, std::vector<std::byte>* _outCapture);

  void Release() noexcept;

  std::shared_ptr<GraphicsCore> m_core;
  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
