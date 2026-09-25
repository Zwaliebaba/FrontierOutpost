// Tests/NeuronClientTests/WarpDevice.cpp
//
// The runner can run Direct3D 12: a device on WARP, the software adapter every Windows has, clears a
// texture, and the texture is read back (plan Phase 2 step 1). NeuronClient's own device comes with
// Phase 3; this uses the API directly.
#include "pch.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include "Check.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Microsoft::WRL::ComPtr;

constexpr UINT TEXTURE_SIZE_PIXELS = 4;
constexpr std::size_t BYTES_PER_PIXEL = 4;

// The clear color, and the bytes R8G8B8A8_UNORM stores for it: each channel times 255, rounded.
constexpr std::array<float, 4> CLEAR_COLOR = {1.0f, 0.2f, 0.6f, 1.0f};
constexpr std::array<std::uint8_t, 4> CLEAR_BYTES = {255, 51, 153, 255};

// Every field stated: D3D12_HEAP_TYPE has no zero value, so {} would not be a heap type.
D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE _type)
{
  return D3D12_HEAP_PROPERTIES{_type, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* _resource, D3D12_RESOURCE_STATES _before, D3D12_RESOURCE_STATES _after)
{
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = _resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = _before;
  barrier.Transition.StateAfter = _after;
  return barrier;
}

} // namespace

TEST_CLASS(WarpDevice)
{
public:
  TEST_METHOD(ClearsATextureAndReadsItBack)
  {
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), L"CreateDXGIFactory2");
    ComPtr<IDXGIAdapter> warp;
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), L"EnumWarpAdapter");
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), L"D3D12CreateDevice on WARP");

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), L"CreateCommandQueue");
    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), L"CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> commands;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)),
          L"CreateCommandList");

    // The texture, as a render target.
    const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = TEXTURE_SIZE_PIXELS;
    textureDesc.Height = TEXTURE_SIZE_PIXELS;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> texture;
    Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                          IID_PPV_ARGS(&texture)),
          L"CreateCommittedResource for the texture");

    D3D12_DESCRIPTOR_HEAP_DESC targetHeapDesc{};
    targetHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    targetHeapDesc.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> targetHeap;
    Check(device->CreateDescriptorHeap(&targetHeapDesc, IID_PPV_ARGS(&targetHeap)), L"CreateDescriptorHeap");
    const D3D12_CPU_DESCRIPTOR_HANDLE target = targetHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(texture.Get(), nullptr, target);

    // Where the copy lands: a buffer the CPU can read, its rows padded to the pitch alignment.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 readbackBytes = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, nullptr, nullptr, &readbackBytes);
    const D3D12_HEAP_PROPERTIES readbackHeap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = readbackBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    Check(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(&readback)),
          L"CreateCommittedResource for the readback buffer");

    commands->ClearRenderTargetView(target, CLEAR_COLOR.data(), 0, nullptr);
    const D3D12_RESOURCE_BARRIER toCopySource =
      Transition(texture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands->ResourceBarrier(1, &toCopySource);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = texture.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    Check(commands->Close(), L"Close");
    const std::array<ID3D12CommandList*, 1> lists = {commands.Get()};
    queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());

    // With no event to signal, SetEventOnCompletion returns once the fence reaches the value.
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), L"CreateFence");
    Check(queue->Signal(fence.Get(), 1), L"Signal");
    Check(fence->SetEventOnCompletion(1, nullptr), L"SetEventOnCompletion");

    // Copied out and unmapped before anything is asserted, so a failure cannot leave it mapped.
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackBytes));
    void* mapped = nullptr;
    const D3D12_RANGE readRange{0, bytes.size()};
    Check(readback->Map(0, &readRange, &mapped), L"Map");
    std::memcpy(bytes.data(), mapped, bytes.size());
    const D3D12_RANGE nothingWritten{0, 0};
    readback->Unmap(0, &nothingWritten);

    std::wstring mismatches;
    const auto offset = static_cast<std::size_t>(footprint.Offset);
    const auto rowPitchBytes = static_cast<std::size_t>(footprint.Footprint.RowPitch);
    for (std::size_t row = 0; row < TEXTURE_SIZE_PIXELS; ++row)
    {
      for (std::size_t column = 0; column < TEXTURE_SIZE_PIXELS; ++column)
      {
        const std::size_t pixel = offset + (row * rowPitchBytes) + (column * BYTES_PER_PIXEL);
        for (std::size_t channel = 0; channel < BYTES_PER_PIXEL; ++channel)
        {
          if (bytes[pixel + channel] != CLEAR_BYTES[channel])
          {
            mismatches +=
              std::format(L"({}, {}) channel {} is {}, not {}; ", column, row, channel, bytes[pixel + channel], CLEAR_BYTES[channel]);
          }
        }
      }
    }
    Assert::IsTrue(mismatches.empty(), mismatches.c_str());
  }
};

} // namespace NeuronClientTests
