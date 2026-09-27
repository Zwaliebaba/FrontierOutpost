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

using winrt::com_ptr;

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

/// The shader models these headers name, newest first. A runtime refuses one newer than it knows,
/// so the question comes down the list; a device beyond the newest answers the newest.
constexpr std::array<D3D_SHADER_MODEL, 8> SHADER_MODELS = {D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5,
                                                           D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2,
                                                           D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0};

/// Asks _device what GraphicsCapabilities names. A question the runtime cannot answer, as an older one
/// cannot, is a no. With the debug layer on, _infoQueue stores nothing meanwhile: a no is an answer
/// here, not a fault, and would otherwise fail every test and smoke run that counts the layer's
/// errors.
GraphicsCapabilities Probe(ID3D12Device& _device, ID3D12InfoQueue* _infoQueue)
{
  std::array<D3D12_MESSAGE_SEVERITY, 5> every = {D3D12_MESSAGE_SEVERITY_CORRUPTION, D3D12_MESSAGE_SEVERITY_ERROR,
                                                 D3D12_MESSAGE_SEVERITY_WARNING, D3D12_MESSAGE_SEVERITY_INFO,
                                                 D3D12_MESSAGE_SEVERITY_MESSAGE};
  D3D12_INFO_QUEUE_FILTER nothing{};
  nothing.DenyList.NumSeverities = static_cast<UINT>(every.size());
  nothing.DenyList.pSeverityList = every.data();
  const bool quiet = _infoQueue != nullptr && SUCCEEDED(_infoQueue->PushStorageFilter(&nothing));

  GraphicsCapabilities capabilities{.shaderModelMajor = 5,
                                    .shaderModelMinor = 1,
                                    .resourceBindingTier = 1,
                                    .directlyIndexedHeaps = false,
                                    .pipelineLibrary = false,
                                    .automaticDiskCache = false};
  for (const D3D_SHADER_MODEL asked : SHADER_MODELS)
  {
    D3D12_FEATURE_DATA_SHADER_MODEL model{asked};
    if (SUCCEEDED(_device.CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))))
    {
      // The major version is the high nibble, the minor the low one: 0x66 is 6.6.
      capabilities.shaderModelMajor = static_cast<std::uint32_t>(model.HighestShaderModel) >> 4;
      capabilities.shaderModelMinor = static_cast<std::uint32_t>(model.HighestShaderModel) & 0xFu;
      break;
    }
  }

  // An out parameter only: its tiers have no zero value for {} to give.
  D3D12_FEATURE_DATA_D3D12_OPTIONS options;
  if (SUCCEEDED(_device.CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
  {
    capabilities.resourceBindingTier = static_cast<std::uint32_t>(options.ResourceBindingTier);
  }

  D3D12_FEATURE_DATA_SHADER_CACHE cache{};
  if (SUCCEEDED(_device.CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &cache, sizeof(cache))))
  {
    capabilities.pipelineLibrary = (cache.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY) != 0;
    capabilities.automaticDiskCache = (cache.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_DISK_CACHE) != 0;
  }

  // From Shader Model 6.6, shaders index the heaps directly when a root signature of version 1.1
  // says they may. Whether such a root signature can be made is the answer.
  const bool shaderModel66 =
    capabilities.shaderModelMajor > 6 || (capabilities.shaderModelMajor == 6 && capabilities.shaderModelMinor >= 6);
  D3D12_FEATURE_DATA_ROOT_SIGNATURE signatureVersion{D3D_ROOT_SIGNATURE_VERSION_1_1};
  if (shaderModel66 && SUCCEEDED(_device.CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &signatureVersion, sizeof(signatureVersion))) &&
      signatureVersion.HighestVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1)
  {
    // D3D_ROOT_SIGNATURE_VERSION has no zero value for {} to give, so both parts are set.
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc;
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1 = D3D12_ROOT_SIGNATURE_DESC1{0, nullptr, 0, nullptr,
                                               D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                                 D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED};
    com_ptr<ID3DBlob> signature;
    com_ptr<ID3DBlob> errors;
    com_ptr<ID3D12RootSignature> rootSignature;
    capabilities.directlyIndexedHeaps =
      SUCCEEDED(D3D12SerializeVersionedRootSignature(&desc, signature.put(), errors.put())) &&
      SUCCEEDED(_device.CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
  }

  if (quiet)
  {
    _infoQueue->PopStorageFilter();
  }
  return capabilities;
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
    com_ptr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
      _error = "Direct3D 12: the debug layer is not installed; it comes with Windows' optional feature Graphics Tools";
      return false;
    }
    debug->EnableDebugLayer();
    if (_desc.gpuValidation)
    {
      com_ptr<ID3D12Debug1> debug1;
      if (FAILED(debug->QueryInterface(IID_PPV_ARGS(&debug1))))
      {
        _error = "Direct3D 12: GPU-based validation is not available with this debug layer";
        return false;
      }
      debug1->SetEnableGPUBasedValidation(TRUE);
    }
  }
  // Without DRED (before Windows 10 1903), a removal is still reported, only with less to say.
  com_ptr<ID3D12DeviceRemovedExtendedDataSettings> dred;
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
  // The adapter DXGI ranks first for performance: a laptop's discrete GPU, where adapter 0 is
  // usually the integrated one (Design/ADR/ADR-007).
  result = _desc.warp ? core->factory->EnumWarpAdapter(IID_PPV_ARGS(&core->adapter))
                      : core->factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&core->adapter));
  if (FAILED(result))
  {
    return fail(_desc.warp ? "IDXGIFactory4::EnumWarpAdapter" : "IDXGIFactory6::EnumAdapterByGpuPreference", result);
  }
  DXGI_ADAPTER_DESC1 adapterDesc{};
  result = core->adapter->GetDesc1(&adapterDesc);
  if (FAILED(result))
  {
    return fail("IDXGIAdapter1::GetDesc1", result);
  }
  core->adapterName = Utf16ToUtf8(adapterDesc.Description);

  result = D3D12CreateDevice(core->adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&core->device));
  if (FAILED(result))
  {
    return fail(std::format("D3D12CreateDevice on {}", core->adapterName), result);
  }
  if (_desc.debugLayer && SUCCEEDED(core->device->QueryInterface(IID_PPV_ARGS(&core->infoQueue))))
  {
    // Only what is worth failing a test over, or logging: corruption, errors and warnings, less
    // three warnings about what liblt does on purpose. Two say that a clear to a value other than
    // the one given when the resource was made is slower: liblt clears to whatever a pass asks for,
    // so no value given in advance would match. The third says that a pixel shader writes a target
    // the pipeline state does not name, and that the write is discarded: that is how GL discarded
    // what went to a draw buffer with nothing attached, which liblt's depth prepass relies on
    // (plan §5.5).
    std::array<D3D12_MESSAGE_SEVERITY, 2> quiet = {D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE};
    std::array<D3D12_MESSAGE_ID, 3> intended = {D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                                                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                                                D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_RENDERTARGETVIEW_NOT_SET};
    D3D12_INFO_QUEUE_FILTER filter{};
    filter.DenyList.NumSeverities = static_cast<UINT>(quiet.size());
    filter.DenyList.pSeverityList = quiet.data();
    filter.DenyList.NumIDs = static_cast<UINT>(intended.size());
    filter.DenyList.pIDList = intended.data();
    core->storageFilterPushed = SUCCEEDED(core->infoQueue->PushStorageFilter(&filter));
  }
  core->capabilities = Probe(*core->device.get(), core->infoQueue.get());

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
  if (!core->context->Initialize(_error))
  {
    return false;
  }

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

GraphicsCapabilities GraphicsDevice::Capabilities() const noexcept
{
  return m_core ? m_core->capabilities : GraphicsCapabilities{};
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
  return m_core ? Program::Make(m_core, _desc) : Program();
}

SwapChain GraphicsDevice::CreateSwapChain(const SwapChain::Desc& _desc)
{
  return m_core ? SwapChain::Make(m_core, _desc) : SwapChain();
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

std::size_t GraphicsDevice::PipelineStates() const noexcept
{
  return m_core ? m_core->context->PipelineStates() : 0;
}

std::vector<std::string> GraphicsDevice::TakeDebugMessages()
{
  std::vector<std::string> messages;
  if (!m_core || !m_core->infoQueue)
  {
    return messages;
  }
  ID3D12InfoQueue& infoQueue = *m_core->infoQueue.get();
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
