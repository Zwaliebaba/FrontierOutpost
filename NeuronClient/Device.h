#pragma once

namespace Neuron
{

/// The D3D12 adapter, queue and swap chain the client draws through, and the per-frame
/// allocator/fence dance that keeps the CPU at most FRAME_COUNT frames ahead of the GPU.
///
/// Create() throws through winrt::check_hresult on anything that had to succeed, so the caller
/// sees one exception at the composition root rather than a half-built device.
///
/// The per-frame calls are deliberately NOT noexcept, even though nothing in a healthy frame
/// throws. A frame either presents or the device is gone, and a device that is gone goes through
/// Debug.h's Fatal, which throws -- so noexcept here would convert the one error this class
/// exists to report into std::terminate, with no message and no place to catch it. The
/// composition root is the only thing that knows how to tell a person (Debug.h).
///
/// WaitForGpu() is the exception, and it is noexcept because the destructor calls it: it reports
/// nothing and returns early instead.
class Device
{
public:
  /// Triple buffering. Two is enough to avoid a stall only when the GPU never runs long; three is
  /// what the flip model wants and costs one more 1280x720 back buffer.
  static constexpr std::uint32_t FRAME_COUNT = 3;

  Device() = default;
  ~Device();

  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&&) = delete;
  Device& operator=(Device&&) = delete;

  /// Builds the device, the direct queue and a flip-model swap chain sized to the window's client
  /// area. The size is passed rather than measured so that the caller owns the one true statement
  /// of what the back buffer is (Lockstep.cpp, from SceneTarget's constants).
  void Create(HWND _window, std::uint32_t _backBufferWidthPixels, std::uint32_t _backBufferHeightPixels);

  /// Waits until the frame that last used this slot has retired, then resets its allocator and
  /// opens the command list. The returned list is owned by the Device and is valid until
  /// EndFrameAndPresent().
  [[nodiscard]] ID3D12GraphicsCommandList* BeginFrame();

  /// Transitions the back buffer to Present, closes and submits the list, presents, and advances
  /// the fence. Fails loudly on device removal rather than presenting whatever is in the buffer.
  void EndFrameAndPresent();

  /// Blocks until the GPU has drained every frame in flight. Called by the destructor; safe to
  /// call before tearing down anything the command lists still reference.
  void WaitForGpu() noexcept;

  /// Empties the debug layer's message queue into DebugTrace, and does nothing in a Release
  /// build. Errors and corruption already broke at the call that caused them; this is what makes
  /// the layer's warnings and info visible to somebody running the game without a debugger
  /// attached, so that "no debug-layer output" is a claim that can be checked rather than
  /// assumed.
  void DrainDebugMessages();

  [[nodiscard]] ID3D12Device* Handle() const noexcept
  {
    return m_device.get();
  }
  [[nodiscard]] ID3D12CommandQueue* Queue() const noexcept
  {
    return m_queue.get();
  }
  [[nodiscard]] ID3D12Resource* BackBuffer() const noexcept
  {
    return m_backBuffers[m_frameIndex].get();
  }
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE BackBufferView() const noexcept;

  /// Which of the FRAME_COUNT slots this frame owns. Anything that writes per-frame data the GPU
  /// reads -- a mapped vertex buffer, a constant buffer -- keeps one copy per slot and indexes it
  /// with this, which is what stops the CPU overwriting a frame still in flight.
  [[nodiscard]] std::uint32_t FrameIndex() const noexcept
  {
    return m_frameIndex;
  }
  [[nodiscard]] std::uint32_t BackBufferWidthPixels() const noexcept
  {
    return m_backBufferWidthPixels;
  }
  [[nodiscard]] std::uint32_t BackBufferHeightPixels() const noexcept
  {
    return m_backBufferHeightPixels;
  }

private:
  void CreateDeviceAndQueue();
  void CreateSwapChain(HWND _window);
  void CreateFrameResources();
  void MoveToNextFrame();

  /// The one place a removed device is diagnosed. D3D12 reports removal as a *later* call
  /// failing, so the reason has to be read from the device rather than from the HRESULT that
  /// happened to surface it.
  void FailIfDeviceRemoved(HRESULT _result, const char* _what);

  winrt::com_ptr<IDXGIFactory7> m_factory;
  winrt::com_ptr<ID3D12Device> m_device;
  winrt::com_ptr<ID3D12CommandQueue> m_queue;
  winrt::com_ptr<IDXGISwapChain3> m_swapChain;
  winrt::com_ptr<ID3D12DescriptorHeap> m_renderTargetViewHeap;
  std::array<winrt::com_ptr<ID3D12Resource>, FRAME_COUNT> m_backBuffers;
  std::array<winrt::com_ptr<ID3D12CommandAllocator>, FRAME_COUNT> m_allocators;
  std::array<std::uint64_t, FRAME_COUNT> m_fenceValues = {};
  winrt::com_ptr<ID3D12GraphicsCommandList> m_commandList;
  winrt::com_ptr<ID3D12Fence> m_fence;
  /// Null in Release and on a machine without the Graphics Tools feature installed.
  winrt::com_ptr<ID3D12InfoQueue> m_infoQueue;
  winrt::handle m_fenceEvent;
  std::uint32_t m_renderTargetViewSize = 0;
  std::uint32_t m_frameIndex = 0;
  std::uint32_t m_backBufferWidthPixels = 0;
  std::uint32_t m_backBufferHeightPixels = 0;
};

} // namespace Neuron
