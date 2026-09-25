// NeuronClient/GraphicsDevice.cpp
#include "pch.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>

#include "GraphicsDevice.h"
#include "Unicode.h"

#include <array>
#include <deque>
#include <exception>
#include <format>
#include <limits>
#include <string_view>
#include <utility>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// What a fence reads once its device has been removed.
constexpr std::uint64_t REMOVED_FENCE_VALUE = std::numeric_limits<std::uint64_t>::max();

void Report(const std::function<void(const std::string&)>& _to, const std::string& _message)
{
  if (_to)
  {
    _to(_message);
  }
}

std::string_view SeverityName(D3D12_MESSAGE_SEVERITY _severity) noexcept
{
  switch (_severity)
  {
  case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    return "corruption";
  case D3D12_MESSAGE_SEVERITY_ERROR:
    return "error";
  case D3D12_MESSAGE_SEVERITY_WARNING:
    return "warning";
  case D3D12_MESSAGE_SEVERITY_INFO:
    return "info";
  default:
    return "message";
  }
}

bool IsRemoval(HRESULT _result) noexcept
{
  return _result == DXGI_ERROR_DEVICE_REMOVED || _result == DXGI_ERROR_DEVICE_RESET || _result == DXGI_ERROR_DEVICE_HUNG;
}

} // namespace

struct GraphicsDevice::Native
{
  struct Release
  {
    std::uint64_t fenceValue; // runs once the fence has reached this
    std::function<void()> release;
  };

  Desc desc;
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12InfoQueue> infoQueue; // with the debug layer only
  bool storageFilterPushed = false;
  std::string adapterName;

  /// What the next submission signals: work recorded now is done once the fence reaches it.
  std::uint64_t nextFenceValue = 1;
  /// What the last frame to use each slot signaled at its end.
  std::array<std::uint64_t, FRAMES_IN_FLIGHT> frameFenceValues{};
  std::deque<std::uint64_t> endedFrames; // the fence values of frames not yet seen finished
  std::uint64_t framesBegun = 0;
  std::uint64_t framesRetired = 0; // frames seen finished
  bool inFrame = false;
  bool removed = false;
  std::deque<Release> releases; // in the order they were deferred, and so of their fence values

  Native() = default;
  Native(const Native&) = delete;
  Native& operator=(const Native&) = delete;

  /// Nothing may still be running on the GPU when the objects below go. An exception cannot be
  /// reported from here, so one, which can only be memory running out for a message, ends the
  /// program.
  ~Native()
  {
    try
    {
      if (queue && fence)
      {
        WaitFor(Signal());
      }
      RunReleases(REMOVED_FENCE_VALUE);
      if (storageFilterPushed)
      {
        infoQueue->PopStorageFilter();
      }
    }
    catch (...)
    {
      std::terminate();
    }
  }

  /// Reports a failed call, and a removed device once. Returns whether the call succeeded.
  bool Check(HRESULT _result, const char* _call)
  {
    if (SUCCEEDED(_result))
    {
      return true;
    }
    if (IsRemoval(_result))
    {
      ReportRemoval();
    }
    else
    {
      Report(desc.onFailure, std::format("Direct3D 12: {} failed with 0x{:08x}", _call, static_cast<unsigned long>(_result)));
    }
    return false;
  }

  void ReportRemoval()
  {
    if (removed)
    {
      return;
    }
    removed = true;
    const HRESULT reason = device->GetDeviceRemovedReason();
    Report(desc.onFailure, std::format("Direct3D 12: the device was removed (0x{:08x})", static_cast<unsigned long>(reason)));
  }

  /// Marks the queue after everything submitted so far. Returns the value that marks it.
  std::uint64_t Signal()
  {
    const std::uint64_t value = nextFenceValue++;
    Check(queue->Signal(fence.Get(), value), "ID3D12CommandQueue::Signal");
    return value;
  }

  /// The fence's value now. A removed device's fence reads REMOVED_FENCE_VALUE.
  std::uint64_t Completed()
  {
    const std::uint64_t value = fence->GetCompletedValue();
    if (value == REMOVED_FENCE_VALUE)
    {
      ReportRemoval();
    }
    return value;
  }

  /// Blocks until the fence reaches _value. Without an event, SetEventOnCompletion returns then.
  void WaitFor(std::uint64_t _value)
  {
    if (Completed() < _value)
    {
      Check(fence->SetEventOnCompletion(_value, nullptr), "ID3D12Fence::SetEventOnCompletion");
    }
  }

  void RunReleases(std::uint64_t _completed)
  {
    while (!releases.empty() && releases.front().fenceValue <= _completed)
    {
      const std::function<void()> release = std::move(releases.front().release);
      releases.pop_front();
      release();
    }
  }

  /// Counts the frames the GPU has finished, and runs the releases it has finished with.
  void Retire()
  {
    const std::uint64_t completed = Completed();
    while (!endedFrames.empty() && endedFrames.front() <= completed)
    {
      endedFrames.pop_front();
      ++framesRetired;
    }
    RunReleases(completed);
  }
};

bool GraphicsDevice::Create(const Desc& _desc, GraphicsDevice& _outDevice, std::string& _error)
{
  const auto fail = [&_error](std::string_view _call, HRESULT _result)
  {
    _error = std::format("Direct3D 12: {} failed with 0x{:08x}", _call, static_cast<unsigned long>(_result));
    return false;
  };

  // The debug layer and DRED apply to devices created after they are set.
  if (_desc.debugLayer)
  {
    ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
      _error = "Direct3D 12: the debug layer is not installed; it comes with Windows' optional feature Graphics Tools";
      return false;
    }
    debug->EnableDebugLayer();
    if (_desc.gpuValidation)
    {
      ComPtr<ID3D12Debug1> debug1;
      if (FAILED(debug.As(&debug1)))
      {
        _error = "Direct3D 12: GPU-based validation is not available with this debug layer";
        return false;
      }
      debug1->SetEnableGPUBasedValidation(TRUE);
    }
  }
  // Without DRED (before Windows 10 1903), a removal is still reported, only with less to say.
  ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
  {
    dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
  }

  auto native = std::make_unique<Native>();
  native->desc = _desc;
  HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(&native->factory));
  if (FAILED(result))
  {
    return fail("CreateDXGIFactory2", result);
  }
  result =
    _desc.warp ? native->factory->EnumWarpAdapter(IID_PPV_ARGS(&native->adapter)) : native->factory->EnumAdapters1(0, &native->adapter);
  if (FAILED(result))
  {
    return fail(_desc.warp ? "IDXGIFactory4::EnumWarpAdapter" : "IDXGIFactory1::EnumAdapters1", result);
  }
  DXGI_ADAPTER_DESC1 adapterDesc{};
  result = native->adapter->GetDesc1(&adapterDesc);
  if (FAILED(result))
  {
    return fail("IDXGIAdapter1::GetDesc1", result);
  }
  native->adapterName = Utf16ToUtf8(adapterDesc.Description);

  result = D3D12CreateDevice(native->adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&native->device));
  if (FAILED(result))
  {
    return fail(std::format("D3D12CreateDevice on {}", native->adapterName), result);
  }
  if (_desc.debugLayer && SUCCEEDED(native->device.As(&native->infoQueue)))
  {
    // Only what is worth failing a test over, or logging: corruption, errors and warnings.
    std::array<D3D12_MESSAGE_SEVERITY, 2> quiet = {D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE};
    D3D12_INFO_QUEUE_FILTER filter{};
    filter.DenyList.NumSeverities = static_cast<UINT>(quiet.size());
    filter.DenyList.pSeverityList = quiet.data();
    native->storageFilterPushed = SUCCEEDED(native->infoQueue->PushStorageFilter(&filter));
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  result = native->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&native->queue));
  if (FAILED(result))
  {
    return fail("ID3D12Device::CreateCommandQueue", result);
  }
  result = native->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&native->fence));
  if (FAILED(result))
  {
    return fail("ID3D12Device::CreateFence", result);
  }
  native->queue->SetName(L"NeuronClient direct queue");
  native->fence->SetName(L"NeuronClient fence");

  _outDevice.m_native = std::move(native);
  return true;
}

GraphicsDevice::GraphicsDevice() noexcept = default;
GraphicsDevice::~GraphicsDevice() = default;
GraphicsDevice::GraphicsDevice(GraphicsDevice&& _other) noexcept = default;
GraphicsDevice& GraphicsDevice::operator=(GraphicsDevice&& _other) noexcept = default;

GraphicsDevice::operator bool() const noexcept
{
  return m_native != nullptr;
}

std::string GraphicsDevice::AdapterName() const
{
  return m_native ? m_native->adapterName : std::string();
}

bool GraphicsDevice::IsDebugLayerOn() const noexcept
{
  return m_native && m_native->desc.debugLayer;
}

void GraphicsDevice::BeginFrame()
{
  if (!m_native)
  {
    return;
  }
  Native& native = *m_native;
  if (native.inFrame)
  {
    Report(native.desc.onFailure, "Direct3D 12: BeginFrame was called again before EndFrame");
    return;
  }
  // The frame FRAMES_IN_FLIGHT before this one used the same slot.
  native.WaitFor(native.frameFenceValues[native.framesBegun % FRAMES_IN_FLIGHT]);
  native.Retire();
  ++native.framesBegun;
  native.inFrame = true;
}

void GraphicsDevice::EndFrame()
{
  if (!m_native)
  {
    return;
  }
  Native& native = *m_native;
  if (!native.inFrame)
  {
    Report(native.desc.onFailure, "Direct3D 12: EndFrame was called without BeginFrame");
    return;
  }
  const std::uint64_t value = native.Signal();
  native.frameFenceValues[(native.framesBegun - 1) % FRAMES_IN_FLIGHT] = value;
  native.endedFrames.push_back(value);
  native.inFrame = false;
}

void GraphicsDevice::WaitIdle()
{
  if (!m_native)
  {
    return;
  }
  m_native->WaitFor(m_native->Signal());
  m_native->Retire();
}

std::uint64_t GraphicsDevice::FramesBegun() const noexcept
{
  return m_native ? m_native->framesBegun : 0;
}

std::uint64_t GraphicsDevice::FramesCompleted() const noexcept
{
  if (!m_native)
  {
    return 0;
  }
  // Frames finish in order, so those the GPU has finished are the front of the ended ones.
  const std::uint64_t completed = m_native->fence->GetCompletedValue();
  std::uint64_t frames = m_native->framesRetired;
  for (const std::uint64_t value : m_native->endedFrames)
  {
    if (value > completed)
    {
      break;
    }
    ++frames;
  }
  return frames;
}

void GraphicsDevice::DeferRelease(std::function<void()> _release)
{
  if (!_release)
  {
    return;
  }
  if (!m_native)
  {
    _release();
    return;
  }
  m_native->releases.push_back({m_native->nextFenceValue, std::move(_release)});
}

std::size_t GraphicsDevice::PendingReleases() const noexcept
{
  return m_native ? m_native->releases.size() : 0;
}

std::vector<std::string> GraphicsDevice::TakeDebugMessages()
{
  std::vector<std::string> messages;
  if (!m_native || !m_native->infoQueue)
  {
    return messages;
  }
  ID3D12InfoQueue& infoQueue = *m_native->infoQueue.Get();
  const UINT64 count = infoQueue.GetNumStoredMessages();
  for (UINT64 index = 0; index < count; ++index)
  {
    // A message is a D3D12_MESSAGE followed by its text, in one block of the size asked for.
    SIZE_T sizeBytes = 0;
    if (FAILED(infoQueue.GetMessage(index, nullptr, &sizeBytes)))
    {
      continue;
    }
    std::vector<std::byte> block(sizeBytes);
    auto* message = reinterpret_cast<D3D12_MESSAGE*>(block.data());
    if (FAILED(infoQueue.GetMessage(index, message, &sizeBytes)))
    {
      continue;
    }
    messages.push_back(std::format("{} {}: {}", SeverityName(message->Severity), static_cast<int>(message->ID), message->pDescription));
  }
  infoQueue.ClearStoredMessages();
  return messages;
}

} // namespace Neuron
