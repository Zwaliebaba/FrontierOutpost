// Tests/NeuronClientTests/DebugEvents.cpp
//
// The context's regions for PIX nest, and stay open across a submission, with each command list
// whole: the debug layer sees nothing wrong. An EndEvent with none open is reported (plan §5.3).
// What PIX shows of them can only be seen in PIX.
#include "pch.h"

#include "Check.h"
#include "DrawContext.h"
#include "ExactColors.h"
#include "GraphicsDevice.h"
#include "TestDevice.h"
#include "Texture.h"

#include <cstddef>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Neuron::Texture;
using Neuron::TextureDimension;
using Neuron::TextureFormat;

Texture MakeTarget(TestDevice& _test)
{
  Texture texture = _test.device.CreateTexture({.dimension = TextureDimension::Texture2D,
                                                .format = TextureFormat::Rgba8,
                                                .widthPixels = 4,
                                                .heightPixels = 4,
                                                .depthPixels = 1,
                                                .mipLevels = 1,
                                                .name = "DebugEvents"});
  Assert::IsTrue(static_cast<bool>(texture), L"the texture was not made");
  return texture;
}

} // namespace

TEST_CLASS(DebugEvents)
{
public:
  TEST_METHOD(NestsRegionsAcrossASubmission)
  {
    TestDevice test;
    Open(test);
    Texture target = MakeTarget(test);
    Neuron::DrawContext& context = test.device.Context();
    test.device.BeginFrame();
    context.BeginEvent("Frame");
    context.BeginEvent("Clear");
    context.ClearColor(target, 0, 0, UNORM_COLOR);
    context.EndEvent();
    // The readback submits the list with Frame still open; the next list begins it again.
    std::vector<std::byte> texels;
    Assert::IsTrue(context.ReadTexture(target, 0, 0, texels), L"ReadTexture failed");
    Assert::IsTrue(texels == ExactTexels(TextureFormat::Rgba8, 16), L"the clear inside the regions was not made");
    context.BeginEvent("Clear again");
    context.ClearColor(target, 0, 0, UNORM_COLOR);
    context.EndEvent();
    context.EndEvent();
    test.device.EndFrame();
    test.device.WaitIdle();
    ExpectClean(test);
  }

  TEST_METHOD(ReportsAnEndWithNoneOpen)
  {
    TestDevice test;
    Open(test);
    Neuron::DrawContext& context = test.device.Context();
    context.BeginEvent("Only");
    context.EndEvent();
    context.EndEvent();
    Assert::AreEqual(std::size_t{1}, test.failures.size(), L"the second EndEvent was not reported");
    test.failures.clear();
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
