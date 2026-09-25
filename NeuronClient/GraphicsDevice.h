// NeuronClient/GraphicsDevice.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Neuron
{

/// Direct3D 12 at feature level 11_0, on the default adapter or on WARP, with one direct queue and
/// two frames in flight (Design/ADR/ADR-007). The CPU records a frame while the GPU runs the one
/// before it, and waits only when it would get further ahead than that. Anything the GPU may still
/// be using is released once the GPU has finished with it, never before. It is not thread-safe:
/// one thread creates it, runs its frames and destroys it.
class GraphicsDevice
{
public:
  /// Frames the CPU may have submitted that the GPU has not finished.
  static constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;

  struct Desc
  {
    bool warp;          // WARP, Windows' software adapter, instead of the default adapter
    bool debugLayer;    // the Direct3D 12 debug layer, which liblt turns on in Debug
    bool gpuValidation; // GPU-based validation on top of the debug layer: slow, so behind a switch
    /// A call failed, or the device was removed. liblt ends the program here (ADR-007): nothing
    /// that depends on the device is recovered.
    std::function<void(const std::string&)> onFailure;
  };

  /// Creates the device and its queue. The debug layer, when asked for, is required: without
  /// Windows' Graphics Tools it is not installed, and Create fails and says so. On failure
  /// returns false and says why in _error.
  [[nodiscard]] static bool Create(const Desc& _desc, GraphicsDevice& _outDevice, std::string& _error);

  GraphicsDevice() noexcept;
  /// Waits for the GPU to finish, then runs every release still waiting.
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

  /// Starts the next frame. First waits until the GPU has finished the frame FRAMES_IN_FLIGHT
  /// before it, then runs the releases the GPU has finished with.
  void BeginFrame();

  /// Ends the frame: submits it, and marks the queue so that its end can be waited for.
  void EndFrame();

  /// Waits until the GPU has finished everything submitted, then runs every release still waiting.
  void WaitIdle();

  /// Frames begun so far.
  [[nodiscard]] std::uint64_t FramesBegun() const noexcept;

  /// Frames the GPU has finished. Once frame n has begun, at least n - FRAMES_IN_FLIGHT have.
  [[nodiscard]] std::uint64_t FramesCompleted() const noexcept;

  /// Runs _release once the GPU has finished all the work submitted so far and all the work
  /// recorded for the current frame. Releases run in the order they were deferred, at the start
  /// of a frame, in WaitIdle, or when the device is destroyed. The core releases its own objects
  /// this way, so that nothing the GPU may still read goes away under it.
  void DeferRelease(std::function<void()> _release);

  /// Releases waiting for the GPU.
  [[nodiscard]] std::size_t PendingReleases() const noexcept;

  /// The messages the debug layer stored since the last call, oldest first: corruption, errors
  /// and warnings, each with its severity and ID. Empty without the debug layer.
  [[nodiscard]] std::vector<std::string> TakeDebugMessages();

private:
  struct Native;

  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
