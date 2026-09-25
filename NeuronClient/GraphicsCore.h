// NeuronClient/GraphicsCore.h
//
// NeuronClient's own: the Direct3D 12 objects behind GraphicsDevice, Texture and Buffer, for the
// core's sources (Design/ADR/ADR-007). Include it after pch.h. No public header includes it,
// because liblt defines names the Windows headers take as macros (Design/ADR/ADR-005).
#pragma once

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>

#include "Buffer.h"
#include "DrawContext.h"
#include "GraphicsDevice.h"
#include "Texture.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

/// What a fence reads once its device has been removed.
inline constexpr std::uint64_t REMOVED_FENCE_VALUE = std::numeric_limits<std::uint64_t>::max();

/// Every field stated: D3D12_HEAP_TYPE has no zero value, so {} would not be a heap type.
[[nodiscard]] inline D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE _type) noexcept
{
  return D3D12_HEAP_PROPERTIES{_type, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
}

/// A buffer resource's description.
[[nodiscard]] D3D12_RESOURCE_DESC BufferDescription(std::uint64_t _sizeBytes) noexcept;

/// The format a texture's resource is made in: depth is typeless, so that it can be sampled too.
[[nodiscard]] DXGI_FORMAT ResourceFormat(TextureFormat _format) noexcept;

/// A GraphicsDevice's Direct3D 12 objects, its fence, its frames and the releases waiting for the
/// GPU. The device owns it, and every texture and buffer made on it shares it, so that it outlives
/// them. What a release holds never holds the core, or the core could never go.
struct GraphicsCore
{
  struct Release
  {
    std::uint64_t fenceValue; // runs once the fence has reached this
    std::function<void()> release;
  };

  GraphicsDevice::Desc desc;
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue; // with the debug layer only
  bool storageFilterPushed = false;
  std::string adapterName;

  /// What the next submission signals: work recorded now is done once the fence reaches it.
  std::uint64_t nextFenceValue = 1;
  std::uint64_t lastSignaled = 0;
  /// What the last frame to use each slot signaled at its end.
  std::array<std::uint64_t, GraphicsDevice::FRAMES_IN_FLIGHT> frameFenceValues{};
  std::deque<std::uint64_t> endedFrames; // the fence values of frames not yet seen finished
  std::uint64_t framesBegun = 0;
  std::uint64_t framesRetired = 0; // frames seen finished
  bool inFrame = false;
  bool removed = false;
  std::deque<Release> releases; // in the order they were deferred, and so of their fence values

  /// The one context, which records all of the device's work.
  std::unique_ptr<DrawContext> context;

  GraphicsCore() = default;
  GraphicsCore(const GraphicsCore&) = delete;
  GraphicsCore& operator=(const GraphicsCore&) = delete;

  /// Waits for the GPU, drops what was recorded and not submitted, and runs every waiting release.
  ~GraphicsCore();

  void Fail(const std::string& _message) const;

  /// Reports a failed call, and a removed device once. Returns whether the call succeeded.
  bool Check(HRESULT _result, std::string_view _call);

  void ReportRemoval();

  /// Marks the queue after everything submitted so far. Returns the value that marks it.
  std::uint64_t Signal();

  /// The fence's value now. A removed device's fence reads REMOVED_FENCE_VALUE.
  std::uint64_t Completed();

  /// Blocks until the fence reaches _value.
  void WaitFor(std::uint64_t _value);

  /// Runs _release once the GPU has finished all the work submitted and recorded so far.
  void DeferRelease(std::function<void()> _release);

  void RunReleases(std::uint64_t _completed);

  /// Counts the frames the GPU has finished, and runs the releases it has finished with.
  void Retire();
};

/// A texture's resource, and the state of each of its subresources as the context last left it.
/// Textures never rely on implicit promotion, so they never decay: the states stay true across
/// command lists.
struct Texture::Native
{
  Texture::Desc desc; // its name is in name
  std::string name;
  D3D12_RESOURCE_DESC resourceDesc{};
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  std::uint32_t faces = 1;                   // six for a cube, one otherwise
  std::vector<D3D12_RESOURCE_STATES> states; // one per subresource, mip + face * mipLevels
  std::vector<std::uint64_t> copiedLists;    // per subresource, the command list that last copied into it

  [[nodiscard]] std::uint32_t Subresource(std::uint32_t _mip, std::uint32_t _face) const noexcept
  {
    return _mip + (_face * desc.mipLevels);
  }
};

/// A buffer's resource, and its state within one command list: a buffer decays to COMMON when a
/// command list finishes on the GPU.
struct Buffer::Native
{
  Buffer::Desc desc; // its name is in name
  std::string name;
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  std::uint64_t stateList = 0;  // the command list state is from
  std::uint64_t copiedList = 0; // the command list that last copied into it
};

} // namespace Neuron
