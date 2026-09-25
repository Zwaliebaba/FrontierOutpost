// NeuronClient/SwapChain.cpp
#include "pch.h"

#include <dxgi1_5.h>

#include "GraphicsCore.h"
#include "SwapChain.h"
#include "Window.h"

#include <array>
#include <cstring>
#include <exception>
#include <format>
#include <utility>

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// One buffer for the GPU to draw into while the display shows the other (plan §5.3).
constexpr UINT BUFFER_COUNT = 2;

/// Whether DXGI may show a frame torn when vsync is off, as Windows and the display allow.
bool TearingAllowed(IDXGIFactory4& _factory)
{
  ComPtr<IDXGIFactory5> factory;
  BOOL allowed = FALSE;
  return SUCCEEDED(_factory.QueryInterface(IID_PPV_ARGS(&factory))) &&
         SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed, sizeof(allowed))) && allowed == TRUE;
}

} // namespace

/// The swap chain, its buffers and their render-target views.
struct SwapChain::Native
{
  GraphicsCore* core = nullptr;
  ComPtr<IDXGISwapChain3> swapChain;
  std::array<ComPtr<ID3D12Resource>, BUFFER_COUNT> buffers;
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, BUFFER_COUNT> views{};
  std::uint32_t widthPixels = 0;
  std::uint32_t heightPixels = 0;
  bool vsync = false;
  bool tearing = false;

  Native() = default;
  Native(const Native&) = delete;
  Native& operator=(const Native&) = delete;
  Native(Native&&) = delete;
  Native& operator=(Native&&) = delete;

  /// An exception cannot be reported from here, so one, which can only be memory running out, ends
  /// the program.
  ~Native()
  {
    try
    {
      ReleaseBuffers();
    }
    catch (...)
    {
      std::terminate();
    }
  }

  /// The flags it is made with, which ResizeBuffers is given again.
  [[nodiscard]] UINT Flags() const noexcept
  {
    return tearing ? static_cast<UINT>(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) : 0;
  }

  /// Takes the buffers, and makes their views. False when that fails, which the core has reported.
  bool TakeBuffers()
  {
    for (UINT index = 0; index < BUFFER_COUNT; ++index)
    {
      if (!core->Check(swapChain->GetBuffer(index, IID_PPV_ARGS(&buffers[index])), "IDXGISwapChain::GetBuffer") ||
          !core->targetViewPool.Allocate(*core, views[index]))
      {
        return false;
      }
      buffers[index]->SetName(L"NeuronClient back buffer");
      core->device->CreateRenderTargetView(buffers[index].Get(), nullptr, views[index]);
    }
    return true;
  }

  /// Lets the buffers and their views go, as ResizeBuffers requires.
  void ReleaseBuffers()
  {
    for (UINT index = 0; index < BUFFER_COUNT; ++index)
    {
      buffers[index].Reset();
      if (views[index].ptr != 0)
      {
        core->targetViewPool.Free(views[index]);
        views[index] = {};
      }
    }
  }
};

SwapChain SwapChain::Make(const std::shared_ptr<GraphicsCore>& _core, const Desc& _desc)
{
  GraphicsCore& core = *_core;
  void* const window = _desc.window != nullptr ? _desc.window->NativeHandle() : nullptr;
  if (window == nullptr || _desc.widthPixels == 0 || _desc.heightPixels == 0)
  {
    core.Fail(std::format("Direct3D 12: a swap chain of {} by {} cannot be made{}", _desc.widthPixels, _desc.heightPixels,
                          window == nullptr ? " without an open window" : ""));
    return {};
  }
  auto native = std::make_unique<Native>();
  native->core = &core;
  native->widthPixels = _desc.widthPixels;
  native->heightPixels = _desc.heightPixels;
  native->vsync = _desc.vsync;
  native->tearing = TearingAllowed(*core.factory.Get());
  const DXGI_SWAP_CHAIN_DESC1 desc{.Width = _desc.widthPixels,
                                   .Height = _desc.heightPixels,
                                   .Format = BACK_BUFFER_FORMAT,
                                   .Stereo = FALSE,
                                   .SampleDesc = {1, 0},
                                   .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                   .BufferCount = BUFFER_COUNT,
                                   .Scaling = DXGI_SCALING_STRETCH,
                                   .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                   .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
                                   .Flags = native->Flags()};
  const HWND handle = static_cast<HWND>(window);
  ComPtr<IDXGISwapChain1> swapChain;
  // No exclusive fullscreen, which nothing uses: Alt+Enter is DXGI's way into it (plan §5.3).
  if (!core.Check(core.factory->CreateSwapChainForHwnd(core.queue.Get(), handle, &desc, nullptr, nullptr, &swapChain),
                  "IDXGIFactory2::CreateSwapChainForHwnd") ||
      !core.Check(swapChain.As(&native->swapChain), "IDXGISwapChain1::QueryInterface for IDXGISwapChain3") ||
      !core.Check(core.factory->MakeWindowAssociation(handle, DXGI_MWA_NO_ALT_ENTER), "IDXGIFactory::MakeWindowAssociation") ||
      !native->TakeBuffers())
  {
    return {};
  }
  SwapChain made;
  made.m_core = _core;
  made.m_native = std::move(native);
  return made;
}

SwapChain::SwapChain() noexcept = default;

SwapChain::~SwapChain()
{
  Release();
}

SwapChain::SwapChain(SwapChain&& _other) noexcept = default;

SwapChain& SwapChain::operator=(SwapChain&& _other) noexcept
{
  if (this != &_other)
  {
    Release();
    m_core = std::move(_other.m_core);
    m_native = std::move(_other.m_native);
  }
  return *this;
}

/// An exception cannot be reported from here, so one, which can only be memory running out, ends
/// the program.
void SwapChain::Release() noexcept
{
  if (!m_native)
  {
    return;
  }
  try
  {
    m_core->context->Flush();
    m_core->WaitFor(m_core->lastSignaled);
  }
  catch (...)
  {
    std::terminate();
  }
  m_native.reset();
  m_core.reset();
}

SwapChain::operator bool() const noexcept
{
  return m_native != nullptr;
}

std::uint32_t SwapChain::WidthPixels() const noexcept
{
  return m_native ? m_native->widthPixels : 0;
}

std::uint32_t SwapChain::HeightPixels() const noexcept
{
  return m_native ? m_native->heightPixels : 0;
}

bool SwapChain::IsTearingAllowed() const noexcept
{
  return m_native && m_native->tearing;
}

void SwapChain::Resize(std::uint32_t _widthPixels, std::uint32_t _heightPixels)
{
  if (!m_native || _widthPixels == 0 || _heightPixels == 0 ||
      (_widthPixels == m_native->widthPixels && _heightPixels == m_native->heightPixels))
  {
    return;
  }
  Native& native = *m_native;
  GraphicsCore& core = *m_core;
  // Nothing may hold the buffers when they are remade, the GPU included.
  core.context->Flush();
  core.WaitFor(core.lastSignaled);
  native.ReleaseBuffers();
  if (!core.Check(native.swapChain->ResizeBuffers(BUFFER_COUNT, _widthPixels, _heightPixels, BACK_BUFFER_FORMAT, native.Flags()),
                  "IDXGISwapChain::ResizeBuffers") ||
      !native.TakeBuffers())
  {
    // It has no buffers to show, and the GPU has finished with it.
    m_native.reset();
    m_core.reset();
    return;
  }
  native.widthPixels = _widthPixels;
  native.heightPixels = _heightPixels;
}

void SwapChain::Present(const Texture& _image)
{
  static_cast<void>(Show(_image, nullptr));
}

bool SwapChain::PresentAndCapture(const Texture& _image, std::vector<std::byte>& _outTexels)
{
  return Show(_image, &_outTexels);
}

bool SwapChain::Show(const Texture& _image, std::vector<std::byte>* _outCapture)
{
  if (!m_native)
  {
    return false;
  }
  Native& native = *m_native;
  GraphicsCore& core = *m_core;
  const UINT index = native.swapChain->GetCurrentBackBufferIndex();
  PresentTarget target{.buffer = native.buffers[index].Get(),
                       .view = native.views[index],
                       .widthPixels = native.widthPixels,
                       .heightPixels = native.heightPixels,
                       .capture = _outCapture != nullptr,
                       .captureBuffer = nullptr,
                       .captureFootprint = {}};
  if (!core.context->RecordPresent(_image, target))
  {
    return false;
  }
  core.context->Flush();
  // With vsync off, a frame is shown as soon as it is ready, torn where that is allowed (N7).
  const UINT interval = native.vsync ? 1u : 0u;
  const UINT flags = (!native.vsync && native.tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
  if (!core.Check(native.swapChain->Present(interval, flags), "IDXGISwapChain::Present"))
  {
    return false;
  }
  if (_outCapture == nullptr)
  {
    return true;
  }
  core.WaitFor(core.lastSignaled);
  if (core.removed)
  {
    return false;
  }
  const UINT rowPitch = target.captureFootprint.Footprint.RowPitch;
  const std::size_t rowBytes = std::size_t{native.widthPixels} * 4;
  const D3D12_RANGE everything{0, static_cast<SIZE_T>(rowPitch) * native.heightPixels};
  void* mapped = nullptr;
  if (!core.Check(target.captureBuffer->Map(0, &everything, &mapped), "ID3D12Resource::Map for a capture of the back buffer"))
  {
    return false;
  }
  // The back buffer's row 0 is the top of the window.
  std::vector<std::byte> texels(rowBytes * native.heightPixels);
  const auto* const rows = static_cast<const std::byte*>(mapped);
  for (std::uint32_t row = 0; row < native.heightPixels; ++row)
  {
    std::memcpy(&texels[row * rowBytes], rows + (std::size_t{row} * rowPitch), rowBytes);
  }
  const D3D12_RANGE nothingWritten{0, 0};
  target.captureBuffer->Unmap(0, &nothingWritten);
  *_outCapture = std::move(texels);
  return true;
}

} // namespace Neuron
