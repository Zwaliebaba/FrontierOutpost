// NeuronClient/GraphicsDevice.h
#pragma once

#include "Buffer.h"
#include "Program.h"
#include "SwapChain.h"
#include "Texture.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Neuron
{

class DrawContext;
struct GraphicsCore;

/// Direct3D 12 at feature level 11_0, on the default adapter or on WARP, with one direct queue and
/// two frames in flight (Design/ADR/ADR-007). The CPU records a frame while the GPU runs the one
/// before it, and waits only when it would get further ahead than that. Anything the GPU may still
/// be using is released once the GPU has finished with it, never before. Its textures and buffers
/// keep what they need of it alive, so they may outlive it. It is not thread-safe: one thread
/// creates it, runs its frames and destroys it.
class GraphicsDevice
{
public:
  /// Frames the CPU may have submitted that the GPU has not finished.
  static constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;

  static constexpr std::uint32_t DEFAULT_UPLOAD_PAGE_BYTES = 4u << 20;

  static constexpr std::uint32_t DEFAULT_SHADER_DESCRIPTORS = 1u << 16;

  struct Desc
  {
    bool warp;          // WARP, Windows' software adapter, instead of the default adapter
    bool debugLayer;    // the Direct3D 12 debug layer, which liblt turns on in Debug
    bool gpuValidation; // GPU-based validation on top of the debug layer: slow, so behind a switch
    /// A call failed, or the device was removed. liblt ends the program here (ADR-007): nothing
    /// that depends on the device is recovered.
    std::function<void(const std::string&)> onFailure;
    /// The size of each page of the upload ring, which constants, geometry and texture updates
    /// share. An upload larger than a page gets a staging buffer of its own.
    std::uint32_t uploadPageBytes = DEFAULT_UPLOAD_PAGE_BYTES;
    /// The shader-visible views: a table of null views, then the ring each draw's table of
    /// textures is taken from, sixteen at a time. At least three tables' worth.
    std::uint32_t shaderDescriptors = DEFAULT_SHADER_DESCRIPTORS;
  };

  /// Creates the device, its queue and its context. The debug layer, when asked for, is required:
  /// without Windows' Graphics Tools it is not installed, and Create fails and says so. On failure
  /// returns false and says why in _error.
  [[nodiscard]] static bool Create(const Desc& _desc, GraphicsDevice& _outDevice, std::string& _error);

  GraphicsDevice() noexcept;
  /// The device goes once its textures and buffers have: then it waits for the GPU to finish, and
  /// runs every release still waiting.
  ~GraphicsDevice();
  GraphicsDevice(GraphicsDevice&& _other) noexcept;
  GraphicsDevice& operator=(GraphicsDevice&& _other) noexcept;
  GraphicsDevice(const GraphicsDevice&) = delete;
  GraphicsDevice& operator=(const GraphicsDevice&) = delete;

  /// True once Create has succeeded.
  explicit operator bool() const noexcept;

  /// The adapter's description, in UTF-8.
  [[nodiscard]] std::string AdapterName() const;

  [[nodiscard]] bool IsDebugLayerOn() const noexcept;

  /// A texture, or an empty one when it cannot be made, which onFailure is told about.
  [[nodiscard]] Texture CreateTexture(const Texture::Desc& _desc);

  /// A buffer, or an empty one when it cannot be made, which onFailure is told about.
  [[nodiscard]] Buffer CreateBuffer(const Buffer::Desc& _desc);

  /// A program, or an empty one when its stages cannot be bound as they are, which onFailure is
  /// told about.
  [[nodiscard]] Program CreateProgram(const Program::Desc& _desc);

  /// A swap chain on a window, or an empty one when it cannot be made, which onFailure is told
  /// about.
  [[nodiscard]] SwapChain CreateSwapChain(const SwapChain::Desc& _desc);

  /// The context that records the device's work. Only a created device has one.
  [[nodiscard]] DrawContext& Context() noexcept;

  /// Starts the next frame. First waits until the GPU has finished the frame FRAMES_IN_FLIGHT
  /// before it, then runs the releases the GPU has finished with.
  void BeginFrame();

  /// Ends the frame: submits what the context recorded, and marks the queue so that the frame's
  /// end can be waited for.
  void EndFrame();

  /// Submits what the context recorded, waits until the GPU has finished everything, and runs
  /// every release still waiting.
  void WaitIdle();

  /// Frames begun so far.
  [[nodiscard]] std::uint64_t FramesBegun() const noexcept;

  /// Frames the GPU has finished. Once frame n has begun, at least n - FRAMES_IN_FLIGHT have.
  [[nodiscard]] std::uint64_t FramesCompleted() const noexcept;

  /// Runs _release once the GPU has finished all the work submitted so far and all the work
  /// recorded for the current frame. Releases run in the order they were deferred, at the start
  /// of a frame, in WaitIdle, or when the device goes. The core releases its own objects this way,
  /// so that nothing the GPU may still read goes away under it.
  void DeferRelease(std::function<void()> _release);

  /// Releases waiting for the GPU.
  [[nodiscard]] std::size_t PendingReleases() const noexcept;

  /// Pipeline states the context holds: one for each program and state a draw has used, made at
  /// the first such draw.
  [[nodiscard]] std::size_t PipelineStates() const noexcept;

  /// The messages the debug layer stored since the last call, oldest first: corruption, errors
  /// and warnings, each with its severity and ID. Empty without the debug layer.
  [[nodiscard]] std::vector<std::string> TakeDebugMessages();

private:
  std::shared_ptr<GraphicsCore> m_core;
};

} // namespace Neuron
