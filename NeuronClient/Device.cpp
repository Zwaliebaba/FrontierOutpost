// Device.cpp -- D3D12 device, direct queue and flip-model swap chain.
//
// Nothing here knows about the 640x400 paletted screen; that is PaletteTarget's job. This file
// owns exactly the part of the stack that would look the same in any D3D12 program: an adapter,
// a queue, back buffers, and the fence that stops the CPU from running away from the GPU.

#include "pch.h"
#include "Device.h"

#include "D3D12Defaults.h"

namespace Neuron
{

namespace
{

// The swap chain is the only place a color arrives as RGBA. Everything the game draws is a
// palette index; the resolve pass is what turns one into the other (see PaletteTarget).
constexpr DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

/// The debug layer is a capability *probe*, not an error check (Debug.h): a machine without the
/// Graphics Tools optional feature installed is a machine that runs the game fine.
void EnableDebugLayerIfPresent()
{
#if defined(_DEBUG)
  winrt::com_ptr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.put()))))
  {
    debugController->EnableDebugLayer();
  }
  else
  {
    DebugTrace("Device: the D3D12 debug layer is not installed; running without it.\n");
  }
#endif
}

const char* SeverityName(D3D12_MESSAGE_SEVERITY _severity) noexcept
{
  switch (_severity)
  {
  case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    return "CORRUPTION";
  case D3D12_MESSAGE_SEVERITY_ERROR:
    return "ERROR";
  case D3D12_MESSAGE_SEVERITY_WARNING:
    return "WARNING";
  case D3D12_MESSAGE_SEVERITY_INFO:
    return "INFO";
  case D3D12_MESSAGE_SEVERITY_MESSAGE:
    return "MESSAGE";
  default:
    return "UNKNOWN";
  }
}

} // namespace

Device::~Device()
{
  WaitForGpu();
}

void Device::Create(HWND _window, std::uint32_t _backBufferWidthPixels, std::uint32_t _backBufferHeightPixels)
{
  m_backBufferWidthPixels = _backBufferWidthPixels;
  m_backBufferHeightPixels = _backBufferHeightPixels;

  EnableDebugLayerIfPresent();
  CreateDeviceAndQueue();
  CreateSwapChain(_window);
  CreateFrameResources();
}

void Device::CreateDeviceAndQueue()
{
  UINT factoryFlags = 0;
#if defined(_DEBUG)
  factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
  winrt::check_hresult(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(m_factory.put())));

  // High-performance preference rather than adapter 0: on a laptop with switchable graphics,
  // adapter 0 is the integrated part and the discrete one is what the machine has for this.
  for (UINT index = 0;; ++index)
  {
    winrt::com_ptr<IDXGIAdapter1> adapter;
    const HRESULT enumerated =
      m_factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.put()));
    if (enumerated == DXGI_ERROR_NOT_FOUND)
    {
      break;
    }
    winrt::check_hresult(enumerated);

    DXGI_ADAPTER_DESC1 description = {};
    winrt::check_hresult(adapter->GetDesc1(&description));
    if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
    {
      continue;
    }

    // A probe again: an adapter that cannot make a feature-level-11 device is one we skip, not
    // one that fails the program.
    if (SUCCEEDED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_device.put()))))
    {
      DebugTrace(L"Device: using adapter {}.\n", std::wstring_view{description.Description});
      break;
    }
  }

  if (!m_device)
  {
    Fatal("No Direct3D 12 adapter at feature level 11_0. This game is D3D12 only (AGENTS.md R12).");
  }

  // Errors and corruption break at the call that caused them: a wrong barrier or a mismatched
  // root signature otherwise shows up as a picture that is subtly wrong three steps later.
  // Warnings deliberately do NOT break -- they are drained to DebugTrace instead, because a
  // warning is usually a thing to read and decide about rather than a reason to stop, and a
  // break with no debugger attached hangs the process in Windows Error Reporting where nobody
  // can see what it was.
#if defined(_DEBUG)
  if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(m_infoQueue.put()))))
  {
    winrt::check_hresult(m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
    winrt::check_hresult(m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
  }
#endif

  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  winrt::check_hresult(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_queue.put())));
}

void Device::CreateSwapChain(HWND _window)
{
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.Width = m_backBufferWidthPixels;
  swapChainDesc.Height = m_backBufferHeightPixels;
  swapChainDesc.Format = BACK_BUFFER_FORMAT;
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.BufferCount = FRAME_COUNT;
  // DXGI_SCALING_NONE, not STRETCH. The back buffer is exactly the client area, so there is
  // nothing to scale -- and a stretch here is the one place a fractional factor could get back
  // into a presentation that Design/README.md section 1 fixes at a whole number.
  swapChainDesc.Scaling = DXGI_SCALING_NONE;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

  winrt::com_ptr<IDXGISwapChain1> swapChain;
  winrt::check_hresult(m_factory->CreateSwapChainForHwnd(m_queue.get(), _window, &swapChainDesc, nullptr, nullptr, swapChain.put()));

  // The window is a fixed size and this game has no fullscreen mode. Alt+Enter would resize a
  // swap chain whose whole contract is that it is an integer multiple of 640x400.
  winrt::check_hresult(m_factory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER));

  m_swapChain = swapChain.as<IDXGISwapChain3>();
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Device::CreateFrameResources()
{
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = FRAME_COUNT;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  winrt::check_hresult(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_renderTargetViewHeap.put())));
  m_renderTargetViewSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_CPU_DESCRIPTOR_HANDLE view = m_renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
  for (std::uint32_t frame = 0; frame < FRAME_COUNT; ++frame)
  {
    winrt::check_hresult(m_swapChain->GetBuffer(frame, IID_PPV_ARGS(m_backBuffers[frame].put())));
    m_device->CreateRenderTargetView(m_backBuffers[frame].get(), nullptr, view);
    view.ptr += m_renderTargetViewSize;

    winrt::check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_allocators[frame].put())));
  }

  winrt::check_hresult(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[m_frameIndex].get(), nullptr,
                                                   IID_PPV_ARGS(m_commandList.put())));
  winrt::check_hresult(m_commandList->Close());

  winrt::check_hresult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.put())));
  m_fenceValues[m_frameIndex] = 1;

  m_fenceEvent = winrt::handle{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  if (!m_fenceEvent)
  {
    winrt::throw_last_error();
  }
}

D3D12_CPU_DESCRIPTOR_HANDLE Device::BackBufferView() const noexcept
{
  D3D12_CPU_DESCRIPTOR_HANDLE view = m_renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
  view.ptr += static_cast<SIZE_T>(m_frameIndex) * m_renderTargetViewSize;
  return view;
}

ID3D12GraphicsCommandList* Device::BeginFrame()
{
  ID3D12CommandAllocator* allocator = m_allocators[m_frameIndex].get();
  FailIfDeviceRemoved(allocator->Reset(), "ID3D12CommandAllocator::Reset");
  FailIfDeviceRemoved(m_commandList->Reset(allocator, nullptr), "ID3D12GraphicsCommandList::Reset");

  const D3D12_RESOURCE_BARRIER toRenderTarget =
    TransitionBarrier(BackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_commandList->ResourceBarrier(1, &toRenderTarget);

  return m_commandList.get();
}

void Device::EndFrameAndPresent()
{
  const D3D12_RESOURCE_BARRIER toPresent =
    TransitionBarrier(BackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  m_commandList->ResourceBarrier(1, &toPresent);
  FailIfDeviceRemoved(m_commandList->Close(), "ID3D12GraphicsCommandList::Close");

  ID3D12CommandList* lists[] = {m_commandList.get()};
  m_queue->ExecuteCommandLists(1, lists);

  // Present(1, 0): vertical sync. The simulation runs at its own fixed tick and the client
  // interpolates, so there is nothing to gain from an unsynchronized present and one obvious
  // thing to lose.
  FailIfDeviceRemoved(m_swapChain->Present(1, 0), "IDXGISwapChain::Present");

  MoveToNextFrame();
}

void Device::MoveToNextFrame()
{
  const std::uint64_t submitted = m_fenceValues[m_frameIndex];
  FailIfDeviceRemoved(m_queue->Signal(m_fence.get(), submitted), "ID3D12CommandQueue::Signal");

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

  if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
  {
    FailIfDeviceRemoved(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent.get()), "SetEventOnCompletion");
    WaitForSingleObjectEx(m_fenceEvent.get(), INFINITE, FALSE);
  }

  m_fenceValues[m_frameIndex] = submitted + 1;
}

void Device::WaitForGpu() noexcept
{
  if (!m_queue || !m_fence || !m_fenceEvent)
  {
    return;
  }

  const std::uint64_t target = m_fenceValues[m_frameIndex];
  if (FAILED(m_queue->Signal(m_fence.get(), target)))
  {
    return;
  }

  if (m_fence->GetCompletedValue() < target)
  {
    if (FAILED(m_fence->SetEventOnCompletion(target, m_fenceEvent.get())))
    {
      return;
    }
    WaitForSingleObjectEx(m_fenceEvent.get(), INFINITE, FALSE);
  }

  m_fenceValues[m_frameIndex] = target + 1;
}

void Device::DrainDebugMessages()
{
  if (!m_infoQueue)
  {
    return;
  }

  const UINT64 count = m_infoQueue->GetNumStoredMessages();
  std::vector<std::byte> storage;
  for (UINT64 index = 0; index < count; ++index)
  {
    SIZE_T length = 0;
    if (FAILED(m_infoQueue->GetMessage(index, nullptr, &length)))
    {
      continue;
    }

    storage.resize(length);
    auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
    if (FAILED(m_infoQueue->GetMessage(index, message, &length)))
    {
      continue;
    }

    DebugTrace("D3D12 {}: {}\n", SeverityName(message->Severity),
               std::string_view{message->pDescription, message->DescriptionByteLength - 1});
  }

  m_infoQueue->ClearStoredMessages();
}

void Device::FailIfDeviceRemoved(HRESULT _result, const char* _what)
{
  if (SUCCEEDED(_result))
  {
    return;
  }

  // DXGI_ERROR_DEVICE_REMOVED and _RESET both mean the adapter is gone; the useful diagnostic is
  // the reason the device itself records, not the call that happened to notice.
  const HRESULT reason =
    (_result == DXGI_ERROR_DEVICE_REMOVED || _result == DXGI_ERROR_DEVICE_RESET) ? m_device->GetDeviceRemovedReason() : _result;
  Fatal("{} failed with 0x{:08X} (device removed reason 0x{:08X}).", _what, static_cast<std::uint32_t>(_result),
        static_cast<std::uint32_t>(reason));
}

} // namespace Neuron
