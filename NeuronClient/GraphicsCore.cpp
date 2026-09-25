// NeuronClient/GraphicsCore.cpp
#include "pch.h"

#include "GraphicsCore.h"

#include <exception>
#include <format>
#include <utility>

namespace Neuron
{

namespace
{

bool IsRemoval(HRESULT _result) noexcept
{
  return _result == DXGI_ERROR_DEVICE_REMOVED || _result == DXGI_ERROR_DEVICE_RESET || _result == DXGI_ERROR_DEVICE_HUNG;
}

} // namespace

D3D12_RESOURCE_DESC BufferDescription(std::uint64_t _sizeBytes) noexcept
{
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = _sizeBytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return desc;
}

/// Nothing may still be running on the GPU when the objects go. An exception cannot be reported
/// from here, so one, which can only be memory running out for a message, ends the program.
GraphicsCore::~GraphicsCore()
{
  try
  {
    if (queue && fence)
    {
      WaitFor(Signal());
    }
    context.reset();
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

void GraphicsCore::Fail(const std::string& _message) const
{
  if (desc.onFailure)
  {
    desc.onFailure(_message);
  }
}

bool GraphicsCore::Check(HRESULT _result, std::string_view _call)
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
    Fail(std::format("Direct3D 12: {} failed with 0x{:08x}", _call, static_cast<unsigned long>(_result)));
  }
  return false;
}

void GraphicsCore::ReportRemoval()
{
  if (removed)
  {
    return;
  }
  removed = true;
  const HRESULT reason = device->GetDeviceRemovedReason();
  Fail(std::format("Direct3D 12: the device was removed (0x{:08x})", static_cast<unsigned long>(reason)));
}

std::uint64_t GraphicsCore::Signal()
{
  const std::uint64_t value = nextFenceValue++;
  Check(queue->Signal(fence.Get(), value), "ID3D12CommandQueue::Signal");
  lastSignaled = value;
  return value;
}

std::uint64_t GraphicsCore::Completed()
{
  const std::uint64_t value = fence->GetCompletedValue();
  if (value == REMOVED_FENCE_VALUE)
  {
    ReportRemoval();
  }
  return value;
}

void GraphicsCore::WaitFor(std::uint64_t _value)
{
  // Without an event, SetEventOnCompletion returns once the fence reaches the value.
  if (Completed() < _value)
  {
    Check(fence->SetEventOnCompletion(_value, nullptr), "ID3D12Fence::SetEventOnCompletion");
  }
}

void GraphicsCore::DeferRelease(std::function<void()> _release)
{
  if (_release)
  {
    releases.push_back({nextFenceValue, std::move(_release)});
  }
}

void GraphicsCore::RunReleases(std::uint64_t _completed)
{
  while (!releases.empty() && releases.front().fenceValue <= _completed)
  {
    const std::function<void()> release = std::move(releases.front().release);
    releases.pop_front();
    release();
  }
}

void GraphicsCore::Retire()
{
  const std::uint64_t completed = Completed();
  while (!endedFrames.empty() && endedFrames.front() <= completed)
  {
    endedFrames.pop_front();
    ++framesRetired;
  }
  RunReleases(completed);
}

} // namespace Neuron
