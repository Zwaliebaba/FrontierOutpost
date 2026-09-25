// NeuronClient/GraphicsDevice.cpp
#include "pch.h"

#include "GraphicsCore.h"
#include "Unicode.h"

#include <array>
#include <format>
#include <string_view>
#include <utility>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

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

} // namespace

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

  auto core = std::make_shared<GraphicsCore>();
  core->desc = _desc;
  HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(&core->factory));
  if (FAILED(result))
  {
    return fail("CreateDXGIFactory2", result);
  }
  result = _desc.warp ? core->factory->EnumWarpAdapter(IID_PPV_ARGS(&core->adapter)) : core->factory->EnumAdapters1(0, &core->adapter);
  if (FAILED(result))
  {
    return fail(_desc.warp ? "IDXGIFactory4::EnumWarpAdapter" : "IDXGIFactory1::EnumAdapters1", result);
  }
  DXGI_ADAPTER_DESC1 adapterDesc{};
  result = core->adapter->GetDesc1(&adapterDesc);
  if (FAILED(result))
  {
    return fail("IDXGIAdapter1::GetDesc1", result);
  }
  core->adapterName = Utf16ToUtf8(adapterDesc.Description);

  result = D3D12CreateDevice(core->adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&core->device));
  if (FAILED(result))
  {
    return fail(std::format("D3D12CreateDevice on {}", core->adapterName), result);
  }
  if (_desc.debugLayer && SUCCEEDED(core->device.As(&core->infoQueue)))
  {
    // Only what is worth failing a test over, or logging: corruption, errors and warnings, less the
    // two warnings that a clear to a value other than the one given when the resource was made is
    // slower. liblt clears to whatever a pass asks for, so no value given in advance would match.
    std::array<D3D12_MESSAGE_SEVERITY, 2> quiet = {D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE};
    std::array<D3D12_MESSAGE_ID, 2> slowClears = {D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                                                  D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE};
    D3D12_INFO_QUEUE_FILTER filter{};
    filter.DenyList.NumSeverities = static_cast<UINT>(quiet.size());
    filter.DenyList.pSeverityList = quiet.data();
    filter.DenyList.NumIDs = static_cast<UINT>(slowClears.size());
    filter.DenyList.pIDList = slowClears.data();
    core->storageFilterPushed = SUCCEEDED(core->infoQueue->PushStorageFilter(&filter));
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  result = core->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&core->queue));
  if (FAILED(result))
  {
    return fail("ID3D12Device::CreateCommandQueue", result);
  }
  result = core->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&core->fence));
  if (FAILED(result))
  {
    return fail("ID3D12Device::CreateFence", result);
  }
  core->queue->SetName(L"NeuronClient direct queue");
  core->fence->SetName(L"NeuronClient fence");
  core->context = std::unique_ptr<DrawContext>(new DrawContext(*core));

  _outDevice.m_core = std::move(core);
  return true;
}

GraphicsDevice::GraphicsDevice() noexcept = default;
GraphicsDevice::~GraphicsDevice() = default;
GraphicsDevice::GraphicsDevice(GraphicsDevice&& _other) noexcept = default;
GraphicsDevice& GraphicsDevice::operator=(GraphicsDevice&& _other) noexcept = default;

GraphicsDevice::operator bool() const noexcept
{
  return m_core != nullptr;
}

std::string GraphicsDevice::AdapterName() const
{
  return m_core ? m_core->adapterName : std::string();
}

bool GraphicsDevice::IsDebugLayerOn() const noexcept
{
  return m_core && m_core->desc.debugLayer;
}

Texture GraphicsDevice::CreateTexture(const Texture::Desc& _desc)
{
  return m_core ? Texture::Make(m_core, _desc) : Texture();
}

Buffer GraphicsDevice::CreateBuffer(const Buffer::Desc& _desc)
{
  return m_core ? Buffer::Make(m_core, _desc) : Buffer();
}

Program GraphicsDevice::CreateProgram(const Program::Desc& _desc)
{
  return m_core ? Program::Make(*m_core, _desc) : Program();
}

DrawContext& GraphicsDevice::Context() noexcept
{
  return *m_core->context;
}

void GraphicsDevice::BeginFrame()
{
  if (!m_core)
  {
    return;
  }
  GraphicsCore& core = *m_core;
  if (core.inFrame)
  {
    core.Fail("Direct3D 12: BeginFrame was called again before EndFrame");
    return;
  }
  // The frame FRAMES_IN_FLIGHT before this one used the same slot.
  core.WaitFor(core.frameFenceValues[core.framesBegun % FRAMES_IN_FLIGHT]);
  core.Retire();
  ++core.framesBegun;
  core.inFrame = true;
}

void GraphicsDevice::EndFrame()
{
  if (!m_core)
  {
    return;
  }
  GraphicsCore& core = *m_core;
  if (!core.inFrame)
  {
    core.Fail("Direct3D 12: EndFrame was called without BeginFrame");
    return;
  }
  core.context->Flush();
  core.frameFenceValues[(core.framesBegun - 1) % FRAMES_IN_FLIGHT] = core.lastSignaled;
  core.endedFrames.push_back(core.lastSignaled);
  core.inFrame = false;
}

void GraphicsDevice::WaitIdle()
{
  if (!m_core)
  {
    return;
  }
  m_core->context->Flush();
  m_core->WaitFor(m_core->lastSignaled);
  m_core->Retire();
}

std::uint64_t GraphicsDevice::FramesBegun() const noexcept
{
  return m_core ? m_core->framesBegun : 0;
}

std::uint64_t GraphicsDevice::FramesCompleted() const noexcept
{
  if (!m_core)
  {
    return 0;
  }
  // Frames finish in order, so those the GPU has finished are the front of the ended ones.
  const std::uint64_t completed = m_core->fence->GetCompletedValue();
  std::uint64_t frames = m_core->framesRetired;
  for (const std::uint64_t value : m_core->endedFrames)
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
  if (!m_core)
  {
    if (_release)
    {
      _release();
    }
    return;
  }
  m_core->DeferRelease(std::move(_release));
}

std::size_t GraphicsDevice::PendingReleases() const noexcept
{
  return m_core ? m_core->releases.size() : 0;
}

std::vector<std::string> GraphicsDevice::TakeDebugMessages()
{
  std::vector<std::string> messages;
  if (!m_core || !m_core->infoQueue)
  {
    return messages;
  }
  ID3D12InfoQueue& infoQueue = *m_core->infoQueue.Get();
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
