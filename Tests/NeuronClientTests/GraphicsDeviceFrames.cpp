// Tests/NeuronClientTests/GraphicsDeviceFrames.cpp
//
// Neuron::GraphicsDevice on WARP with the debug layer: the runner has the layer, so that the rest of
// Phase 3's tests can count its messages; what it sees reaches TakeDebugMessages; the CPU keeps at
// most two frames ahead of the GPU; and a release waits for the GPU (Design/ADR/ADR-007).
#include "pch.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include "Check.h"
#include "GraphicsDevice.h"
#include "TestDevice.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Microsoft::WRL::ComPtr;

} // namespace

TEST_CLASS(GraphicsDeviceFrames)
{
public:
  TEST_METHOD(CreatesAWarpDeviceWithTheDebugLayer)
  {
    TestDevice test;
    Open(test);
    Assert::IsTrue(static_cast<bool>(test.device));
    Assert::IsTrue(test.device.IsDebugLayerOn());
    Assert::IsFalse(test.device.AdapterName().empty());
    ExpectClean(test);
  }

  TEST_METHOD(ReportsWhatTheDebugLayerSees)
  {
    TestDevice test;
    Open(test);
    // The same device as the test device's, since devices are singletons per adapter, asked for a
    // texture with no width.
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), L"CreateDXGIFactory2");
    ComPtr<IDXGIAdapter> warp;
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), L"EnumWarpAdapter");
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), L"D3D12CreateDevice on WARP");
    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 0;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> texture;
    Assert::IsTrue(FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                          IID_PPV_ARGS(&texture))),
                   L"a texture with no width was made");

    const std::vector<std::string> messages = test.device.TakeDebugMessages();
    const bool error = std::ranges::any_of(messages, [](const std::string& _message) { return _message.starts_with("error "); });
    Assert::IsTrue(error, L"the debug layer's error did not reach TakeDebugMessages");
    Assert::IsTrue(test.device.TakeDebugMessages().empty(), L"TakeDebugMessages left what it returned");
    Assert::IsTrue(test.failures.empty());
  }

  TEST_METHOD(KeepsAtMostTwoFramesInFlight)
  {
    TestDevice test;
    Open(test);
    constexpr std::uint64_t FRAMES = 8;
    for (std::uint64_t frame = 1; frame <= FRAMES; ++frame)
    {
      test.device.BeginFrame();
      Assert::AreEqual(frame, test.device.FramesBegun());
      Assert::IsTrue(test.device.FramesCompleted() + Neuron::GraphicsDevice::FRAMES_IN_FLIGHT >= frame,
                     L"the CPU got more than two frames ahead of the GPU");
      test.device.EndFrame();
    }
    test.device.WaitIdle();
    Assert::AreEqual(FRAMES, test.device.FramesCompleted());
    ExpectClean(test);
  }

  TEST_METHOD(ReleasesOnlyOnceTheGpuIsDone)
  {
    TestDevice test;
    Open(test);
    std::vector<int> released;
    test.device.BeginFrame();
    test.device.DeferRelease([&released] { released.push_back(1); });
    test.device.DeferRelease([&released] { released.push_back(2); });
    Assert::AreEqual(std::size_t{2}, test.device.PendingReleases());
    Assert::IsTrue(released.empty(), L"a release ran before its frame was submitted");
    test.device.EndFrame();
    test.device.BeginFrame();
    test.device.EndFrame();
    // The third frame waits for the first, whose releases are then run.
    test.device.BeginFrame();
    Assert::AreEqual(std::size_t{0}, test.device.PendingReleases());
    Assert::IsTrue(released == std::vector<int>{1, 2}, L"the releases did not run in order");
    test.device.EndFrame();
    ExpectClean(test);
  }

  TEST_METHOD(RunsWaitingReleasesWhenDestroyed)
  {
    bool released = false;
    {
      TestDevice test;
      Open(test);
      test.device.DeferRelease([&released] { released = true; });
      Assert::IsFalse(released);
      ExpectClean(test);
    }
    Assert::IsTrue(released, L"the device went without running its waiting release");
  }
};

} // namespace NeuronClientTests
