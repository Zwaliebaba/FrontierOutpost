// NeuronClient/GraphicsCore.h
//
// NeuronClient's own: the Direct3D 12 objects behind GraphicsDevice, Texture, Buffer and Program, for
// the core's sources (Design/ADR/ADR-007). Include it after pch.h. No public header includes it,
// because liblt defines names the Windows headers take as macros (Design/ADR/ADR-005).
#pragma once

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>

#include "Buffer.h"
#include "DrawContext.h"
#include "GraphicsDevice.h"
#include "Program.h"
#include "Texture.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <span>
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

struct GraphicsCore;

/// CPU-only descriptors of one type, handed out one at a time from blocks the pool keeps, and taken
/// back when their resource goes. Views are made in these, and copied into shader-visible heaps
/// when a draw uses them.
struct DescriptorPool
{
  static constexpr UINT BLOCK_DESCRIPTORS = 256;

  explicit DescriptorPool(D3D12_DESCRIPTOR_HEAP_TYPE _type) noexcept
    : type(_type)
  {
  }

  D3D12_DESCRIPTOR_HEAP_TYPE type;
  UINT incrementBytes = 0;
  std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> blocks;
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> available;

  bool Allocate(GraphicsCore& _core, D3D12_CPU_DESCRIPTOR_HANDLE& _outHandle);
  void Free(D3D12_CPU_DESCRIPTOR_HANDLE _handle);
};

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

  DescriptorPool targetViewPool{D3D12_DESCRIPTOR_HEAP_TYPE_RTV};
  DescriptorPool depthViewPool{D3D12_DESCRIPTOR_HEAP_TYPE_DSV};
  DescriptorPool shaderViewPool{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV};
  std::uint64_t nextProgramId = 1; // a program's id keys the pipeline states made for it

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

  /// A texture that goes stops being one of the context's targets.
  void ForgetTexture(const Texture::Native& _texture) noexcept;

  /// A program that goes stops being the context's, and its pipeline states are released once
  /// the GPU has finished with them.
  void ForgetProgram(const Program::Native& _program);
};

/// A texture's resource, and the state of each of its subresources as the context last left it.
/// Textures never rely on implicit promotion, so they never decay: the states stay true across
/// command lists.
struct Texture::Native
{
  /// Not owned: the core outlives every texture's objects, which its releases hold.
  GraphicsCore* core = nullptr;
  Texture::Desc desc; // its name is in name
  std::string name;
  D3D12_RESOURCE_DESC resourceDesc{};
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  std::uint32_t faces = 1;                   // six for a cube, one otherwise
  std::vector<D3D12_RESOURCE_STATES> states; // one per subresource, mip + face * mipLevels
  std::vector<std::uint64_t> copiedLists;    // per subresource, the command list that last copied into it
  /// The render-target views made so far, by mip << 32 | face or slice.
  std::map<std::uint64_t, D3D12_CPU_DESCRIPTOR_HANDLE> targetViews;
  D3D12_CPU_DESCRIPTOR_HANDLE depthView{};  // made when first asked for
  D3D12_CPU_DESCRIPTOR_HANDLE shaderView{}; // made when first asked for

  Native() = default;
  Native(const Native&) = delete;
  Native& operator=(const Native&) = delete;

  /// Hands the views back to the core's pools.
  ~Native();

  [[nodiscard]] std::uint32_t Subresource(std::uint32_t _mip, std::uint32_t _face) const noexcept
  {
    return _mip + (_face * desc.mipLevels);
  }

  /// The render-target view of mip _mip of face or slice _layer, made the first time it is asked
  /// for. False when no descriptor could be had, which the core has reported.
  bool TargetView(std::uint32_t _mip, std::uint32_t _layer, D3D12_CPU_DESCRIPTOR_HANDLE& _outView);

  /// A depth texture's depth-stencil view, made the first time it is asked for.
  bool DepthView(D3D12_CPU_DESCRIPTOR_HANDLE& _outView);

  /// The view the shaders sample through: every mip, of the whole cube or 3D texture, and depth
  /// as R32_FLOAT. Made the first time it is asked for.
  bool ShaderView(D3D12_CPU_DESCRIPTOR_HANDLE& _outView);
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

/// A program's bytecode, and what reflection found in it.
struct Program::Native
{
  /// One stage's $Globals.
  struct Constants
  {
    std::uint32_t sizeBytes = 0;
    std::map<std::string, ProgramConstant, std::less<>> variables;
  };

  std::string name;
  std::uint64_t id = 0; // the core's count, which keys the pipeline states made for the program
  bool compute = false;
  std::vector<std::byte> vertexShader;
  std::vector<std::byte> pixelShader;
  std::vector<std::byte> computeShader;
  std::array<Constants, 3> constants; // by ShaderStage
  /// Each stage's $Globals as the CPU last set it, which each draw uploads.
  std::array<std::vector<std::byte>, 3> constantValues;
  std::map<std::string, int, std::less<>> shaderResources;
  std::map<std::string, int, std::less<>> samplers;
  std::map<std::string, int, std::less<>> unorderedResources;
  std::uint32_t targetCount = 0;
  std::vector<ProgramInput> inputs;

  /// Adds what D3DReflect finds in one stage's bytecode. False, with the reason in _error, for
  /// what the core cannot bind: a cbuffer other than $Globals at b0, a register past its table, a
  /// register space, an array, or a name at two registers.
  bool Reflect(std::span<const std::byte> _bytecode, ShaderStage _stage, std::string& _error);
};

} // namespace Neuron
