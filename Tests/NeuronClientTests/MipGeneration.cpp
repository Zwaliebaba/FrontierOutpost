// Tests/NeuronClientTests/MipGeneration.cpp
//
// GenerateMips makes every mip of 2D and cube textures in the seven colour formats from mip 0, each
// texel the average of the 2 by 2 above it, and of 3 where the mip above is odd in that direction
// and this is the last column or row, so that no texel is left out (Design/ADR/ADR-007; plan §5.3
// and Phase 3).
#include "pch.h"

#include "Check.h"
#include "DrawContext.h"
#include "ExactColors.h"
#include "GraphicsDevice.h"
#include "TestDevice.h"
#include "Texture.h"

#include <algorithm>
#include <array>
#include <cmath>
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

using Neuron::Texture;
using Neuron::TextureDimension;
using Neuron::TextureFormat;

Texture Make(TestDevice& _test, TextureDimension _dimension, TextureFormat _format, std::uint32_t _width, std::uint32_t _height,
             std::uint32_t _depth, std::uint32_t _mips)
{
  Texture texture = _test.device.CreateTexture({.dimension = _dimension,
                                                .format = _format,
                                                .widthPixels = _width,
                                                .heightPixels = _height,
                                                .depthPixels = _depth,
                                                .mipLevels = _mips,
                                                .name = "MipGeneration"});
  Assert::IsTrue(static_cast<bool>(texture), L"the texture was not made");
  return texture;
}

std::vector<std::byte> Read(TestDevice& _test, const Texture& _texture, std::uint32_t _mip, std::uint32_t _face)
{
  std::vector<std::byte> texels;
  Assert::IsTrue(_test.device.Context().ReadTexture(_texture, _mip, _face, texels), L"ReadTexture failed");
  return texels;
}

std::vector<float> ToFloats(const std::vector<std::byte>& _bytes)
{
  std::vector<float> values(_bytes.size() / sizeof(float));
  std::memcpy(values.data(), _bytes.data(), values.size() * sizeof(float));
  return values;
}

/// The mip below _source, _width by _height, as the plan's box makes it.
std::vector<double> NextMip(const std::vector<double>& _source, std::uint32_t _width, std::uint32_t _height)
{
  const std::uint32_t width = std::max(_width / 2, 1u);
  const std::uint32_t height = std::max(_height / 2, 1u);
  std::vector<double> mip;
  for (std::uint32_t y = 0; y < height; ++y)
  {
    for (std::uint32_t x = 0; x < width; ++x)
    {
      const std::uint32_t lastX = x == width - 1 && _width % 2 == 1 ? _width - 1 : std::min((2 * x) + 1, _width - 1);
      const std::uint32_t lastY = y == height - 1 && _height % 2 == 1 ? _height - 1 : std::min((2 * y) + 1, _height - 1);
      double sum = 0.0;
      for (std::uint32_t sourceY = 2 * y; sourceY <= lastY; ++sourceY)
      {
        for (std::uint32_t sourceX = 2 * x; sourceX <= lastX; ++sourceX)
        {
          sum += _source[(std::size_t{sourceY} * _width) + sourceX];
        }
      }
      mip.push_back(sum / static_cast<double>((lastX - (2 * x) + 1) * (lastY - (2 * y) + 1)));
    }
  }
  return mip;
}

} // namespace

TEST_CLASS(MipGeneration)
{
public:
  TEST_METHOD(MakesEveryMipInEveryColorFormat)
  {
    TestDevice test;
    Open(test);
    Neuron::DrawContext& context = test.device.Context();
    for (const TextureFormat format : COLOR_FORMATS)
    {
      // A texture of one colour, which every mip keeps exactly.
      Texture texture = Make(test, TextureDimension::Texture2D, format, 4, 4, 1, Texture::FullMipLevels(4, 4, 1));
      context.UpdateTexture(texture, 0, 0, ExactTexels(format, 16));
      context.GenerateMips(texture);
      for (std::uint32_t mip = 1; mip < texture.MipLevels(); ++mip)
      {
        const std::size_t texels = std::size_t{texture.WidthPixels(mip)} * texture.HeightPixels(mip);
        const std::wstring where = std::format(L"format {}, mip {}", static_cast<int>(format), mip);
        Assert::IsTrue(Read(test, texture, mip, 0) == ExactTexels(format, texels), where.c_str());
      }
    }
    ExpectClean(test);
  }

  TEST_METHOD(AveragesOddSizesAsThePlanSays)
  {
    TestDevice test;
    Open(test);
    Neuron::DrawContext& context = test.device.Context();
    // 7 by 5, then 3 by 2, then 1 by 1, of x + 10 y.
    Texture texture = Make(test, TextureDimension::Texture2D, TextureFormat::R32F, 7, 5, 1, Texture::FullMipLevels(7, 5, 1));
    Assert::AreEqual(3u, texture.MipLevels());
    std::vector<double> expected;
    std::vector<float> values;
    for (std::uint32_t y = 0; y < 5; ++y)
    {
      for (std::uint32_t x = 0; x < 7; ++x)
      {
        values.push_back(static_cast<float>(x + (10 * y)));
        expected.push_back(values.back());
      }
    }
    std::vector<std::byte> texels(values.size() * sizeof(float));
    std::memcpy(texels.data(), values.data(), texels.size());
    context.UpdateTexture(texture, 0, 0, texels);
    context.GenerateMips(texture);
    for (std::uint32_t mip = 1; mip < texture.MipLevels(); ++mip)
    {
      expected = NextMip(expected, texture.WidthPixels(mip - 1), texture.HeightPixels(mip - 1));
      const std::vector<float> made = ToFloats(Read(test, texture, mip, 0));
      Assert::AreEqual(expected.size(), made.size());
      for (std::size_t index = 0; index < made.size(); ++index)
      {
        // Division on the GPU need not round as the CPU's does.
        const std::wstring where = std::format(L"mip {}, texel {}: {} and not {}", mip, index, made[index], expected[index]);
        Assert::IsTrue(std::abs(static_cast<double>(made[index]) - expected[index]) <= 1e-4 * std::max(1.0, std::abs(expected[index])),
                       where.c_str());
      }
    }
    ExpectClean(test);
  }

  TEST_METHOD(MakesTheMipsOfEachFaceOfACube)
  {
    TestDevice test;
    Open(test);
    Neuron::DrawContext& context = test.device.Context();
    Texture cube = Make(test, TextureDimension::TextureCube, TextureFormat::Rgba8, 4, 4, 1, Texture::FullMipLevels(4, 4, 1));
    // Face f is all 30 (f + 1).
    for (std::uint32_t face = 0; face < 6; ++face)
    {
      context.UpdateTexture(cube, 0, face, std::vector<std::byte>(64, static_cast<std::byte>(30 * (face + 1))));
    }
    context.GenerateMips(cube);
    for (std::uint32_t face = 0; face < 6; ++face)
    {
      for (std::uint32_t mip = 1; mip < cube.MipLevels(); ++mip)
      {
        const std::size_t bytes = std::size_t{cube.WidthPixels(mip)} * cube.HeightPixels(mip) * 4;
        const std::wstring where = std::format(L"face {}, mip {}", face, mip);
        Assert::IsTrue(Read(test, cube, mip, face) == std::vector<std::byte>(bytes, static_cast<std::byte>(30 * (face + 1))),
                       where.c_str());
      }
    }
    ExpectClean(test);
  }

  TEST_METHOD(RefusesTexturesWithoutMipsToMake)
  {
    TestDevice test;
    Open(test);
    Neuron::DrawContext& context = test.device.Context();
    Texture volume = Make(test, TextureDimension::Texture3D, TextureFormat::R32F, 4, 4, 4, 2);
    Texture depth = Make(test, TextureDimension::Texture2D, TextureFormat::Depth32F, 4, 4, 1, 1);
    Texture single = Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 4, 4, 1, 1);
    context.GenerateMips(volume);
    context.GenerateMips(depth);
    // A texture with one mip has none to make, which is no fault.
    context.GenerateMips(single);
    Assert::AreEqual(std::size_t{2}, test.failures.size(), L"not every refusal was reported");
    test.failures.clear();
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
