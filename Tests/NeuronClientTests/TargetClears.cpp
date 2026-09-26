// Tests/NeuronClientTests/TargetClears.cpp
//
// DrawContext clears a colour texture's mip, cube face or 3D slice, and nothing beside it, to the
// value its format stores, and clears depth, the whole of a level or a rectangle of it, as GL's
// glClear cleared under the scissor. It clears through views it makes on first use, which stay each
// texture's own until the texture goes, and it refuses what it cannot clear (Design/ADR/ADR-007;
// plan Phase 3 and Phase 4 step 3).
#include "pch.h"

#include "Check.h"
#include "DrawContext.h"
#include "ExactColors.h"
#include "GraphicsDevice.h"
#include "TestDevice.h"
#include "Texture.h"

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

using Neuron::Texture;
using Neuron::TextureDimension;
using Neuron::TextureFormat;

constexpr std::array<float, 4> TRANSPARENT_BLACK = {0.0f, 0.0f, 0.0f, 0.0f};

/// More textures than a block of views holds.
constexpr std::size_t MANY_TEXTURES = 300;

/// _count copies of _value.
std::vector<std::byte> Repeated(float _value, std::size_t _count)
{
  std::vector<std::byte> bytes(_count * sizeof(float));
  for (std::size_t index = 0; index < _count; ++index)
  {
    std::memcpy(&bytes[index * sizeof(float)], &_value, sizeof(_value));
  }
  return bytes;
}

/// _count floats counting up from 1, none of them what a clear leaves.
std::vector<std::byte> Counting(std::size_t _count)
{
  std::vector<std::byte> bytes(_count * sizeof(float));
  for (std::size_t index = 0; index < _count; ++index)
  {
    const float value = static_cast<float>(index) + 1.0f;
    std::memcpy(&bytes[index * sizeof(float)], &value, sizeof(value));
  }
  return bytes;
}

/// The Rgba8 colour that numbers texture _index of MANY_TEXTURES: its low byte in red, its high byte
/// in green.
std::array<float, 4> Numbered(std::size_t _index) noexcept
{
  const auto low = static_cast<float>(_index & 0xFFu);
  const auto high = static_cast<float>(_index >> 8);
  return {low / 255.0f, high / 255.0f, 0.0f, 1.0f};
}

std::vector<std::byte> NumberedTexel(std::size_t _index)
{
  return {static_cast<std::byte>(_index & 0xFFu), static_cast<std::byte>(_index >> 8), std::byte{0}, std::byte{255}};
}

std::size_t LevelTexels(const Texture& _texture, std::uint32_t _mip)
{
  return static_cast<std::size_t>(_texture.WidthPixels(_mip)) * _texture.HeightPixels(_mip) * _texture.DepthPixels(_mip);
}

Texture Make(TestDevice& _test, TextureDimension _dimension, TextureFormat _format, std::uint32_t _width, std::uint32_t _height,
             std::uint32_t _depth, std::uint32_t _mips)
{
  Texture texture = _test.device.CreateTexture({.dimension = _dimension,
                                                .format = _format,
                                                .widthPixels = _width,
                                                .heightPixels = _height,
                                                .depthPixels = _depth,
                                                .mipLevels = _mips,
                                                .name = "TargetClears"});
  Assert::IsTrue(static_cast<bool>(texture), L"the texture was not made");
  return texture;
}

std::vector<std::byte> Read(TestDevice& _test, const Texture& _texture, std::uint32_t _mip, std::uint32_t _face)
{
  std::vector<std::byte> texels;
  Assert::IsTrue(_test.device.Context().ReadTexture(_texture, _mip, _face, texels), L"ReadTexture failed");
  return texels;
}

} // namespace

TEST_CLASS(TargetClears)
{
public:
  TEST_METHOD(ClearsEveryColorFormat)
  {
    TestDevice test;
    Open(test);
    for (const TextureFormat format : COLOR_FORMATS)
    {
      Texture texture = Make(test, TextureDimension::Texture2D, format, 5, 3, 1, 1);
      test.device.Context().ClearColor(texture, 0, 0, ExactColor(format));
      const std::wstring where = std::format(L"format {}", static_cast<int>(format));
      Assert::IsTrue(Read(test, texture, 0, 0) == ExactTexels(format, 15), where.c_str());
    }
    ExpectClean(test);
  }

  TEST_METHOD(ClearsOneMipOfOneCubeFace)
  {
    TestDevice test;
    Open(test);
    Texture texture = Make(test, TextureDimension::TextureCube, TextureFormat::Rgba8, 4, 4, 1, 2);
    Neuron::DrawContext& context = test.device.Context();
    for (std::uint32_t face = 0; face < texture.Faces(); ++face)
    {
      for (std::uint32_t mip = 0; mip < texture.MipLevels(); ++mip)
      {
        context.ClearColor(texture, mip, face, TRANSPARENT_BLACK);
      }
    }
    context.ClearColor(texture, 1, 3, UNORM_COLOR);
    for (std::uint32_t face = 0; face < texture.Faces(); ++face)
    {
      for (std::uint32_t mip = 0; mip < texture.MipLevels(); ++mip)
      {
        const std::size_t texels = LevelTexels(texture, mip);
        const std::vector<std::byte> expected =
          face == 3 && mip == 1 ? ExactTexels(TextureFormat::Rgba8, texels) : std::vector<std::byte>(texels * 4);
        const std::wstring where = std::format(L"face {}, mip {}", face, mip);
        Assert::IsTrue(Read(test, texture, mip, face) == expected, where.c_str());
      }
    }
    ExpectClean(test);
  }

  TEST_METHOD(ClearsOneSliceOfA3DTexture)
  {
    TestDevice test;
    Open(test);
    // Five slices of 4 by 3, filled by a copy first, so that the clear follows one.
    Texture texture = Make(test, TextureDimension::Texture3D, TextureFormat::R32F, 4, 3, 5, 1);
    Neuron::DrawContext& context = test.device.Context();
    context.UpdateTexture(texture, 0, 0, Counting(60));
    context.ClearColor(texture, 0, 2, FLOAT_COLOR);
    std::vector<std::byte> expected = Counting(60);
    const std::vector<std::byte> slice = Repeated(FLOAT_COLOR[0], 12);
    std::memcpy(&expected[2 * slice.size()], slice.data(), slice.size());
    Assert::IsTrue(Read(test, texture, 0, 0) == expected, L"not slice 2 alone was cleared");
    ExpectClean(test);
  }

  TEST_METHOD(ClearsDepth)
  {
    TestDevice test;
    Open(test);
    Texture depth = Make(test, TextureDimension::Texture2D, TextureFormat::Depth32F, 6, 4, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    context.ClearDepth(depth, 0.25f);
    Assert::IsTrue(Read(test, depth, 0, 0) == Repeated(0.25f, 24), L"the first clear did not arrive");
    context.ClearDepth(depth, 1.0f);
    Assert::IsTrue(Read(test, depth, 0, 0) == Repeated(1.0f, 24), L"the second clear did not arrive");
    ExpectClean(test);
  }

  TEST_METHOD(ClearsOnlyTheRectangle)
  {
    TestDevice test;
    Open(test);
    Texture color = Make(test, TextureDimension::Texture2D, TextureFormat::R32F, 4, 4, 1, 1);
    Texture depth = Make(test, TextureDimension::Texture2D, TextureFormat::Depth32F, 4, 4, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    const auto cleared = [](std::vector<std::byte>& _texels, std::size_t _column, std::size_t _row, float _value)
    { std::memcpy(&_texels[((_row * 4) + _column) * sizeof(float)], &_value, sizeof(_value)); };

    // Columns 1 and 2 of row 2, counted from the bottom row, as the scissor counts them.
    context.UpdateTexture(color, 0, 0, Counting(16));
    context.ClearColor(color, 0, 0, TRANSPARENT_BLACK, {.xPixels = 1, .yPixels = 2, .widthPixels = 2, .heightPixels = 1});
    std::vector<std::byte> expected = Counting(16);
    cleared(expected, 1, 2, 0.0f);
    cleared(expected, 2, 2, 0.0f);
    Assert::IsTrue(Read(test, color, 0, 0) == expected, L"not the rectangle alone was cleared");

    // What lies outside the level is left out, and a rectangle wholly outside it clears nothing.
    context.ClearColor(color, 0, 0, TRANSPARENT_BLACK, {.xPixels = 3, .yPixels = -2, .widthPixels = 5, .heightPixels = 3});
    context.ClearColor(color, 0, 0, TRANSPARENT_BLACK, {.xPixels = 4, .yPixels = 0, .widthPixels = 2, .heightPixels = 2});
    cleared(expected, 3, 0, 0.0f);
    Assert::IsTrue(Read(test, color, 0, 0) == expected, L"a rectangle partly outside the level");

    // Depth likewise: the two columns on the left.
    context.ClearDepth(depth, 1.0f);
    context.ClearDepth(depth, 0.25f, {.xPixels = 0, .yPixels = 0, .widthPixels = 2, .heightPixels = 4});
    std::vector<std::byte> depths = Repeated(1.0f, 16);
    for (std::size_t row = 0; row < 4; ++row)
    {
      cleared(depths, 0, row, 0.25f);
      cleared(depths, 1, row, 0.25f);
    }
    Assert::IsTrue(Read(test, depth, 0, 0) == depths, L"the depth rectangle");
    ExpectClean(test);
  }

  TEST_METHOD(GivesEachTextureItsOwnViews)
  {
    TestDevice test;
    Open(test);
    Neuron::DrawContext& context = test.device.Context();
    // The second round takes the views the first handed back.
    for (int round = 0; round < 2; ++round)
    {
      std::vector<Texture> textures;
      for (std::size_t index = 0; index < MANY_TEXTURES; ++index)
      {
        textures.push_back(Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 1, 1, 1, 1));
        context.ClearColor(textures.back(), 0, 0, TRANSPARENT_BLACK);
      }
      // A second clear goes through the view the first made, and reaches its own texture alone.
      for (std::size_t index = 0; index < MANY_TEXTURES; ++index)
      {
        context.ClearColor(textures[index], 0, 0, Numbered(index));
      }
      for (std::size_t index = 0; index < MANY_TEXTURES; ++index)
      {
        const std::wstring where = std::format(L"round {}, texture {}", round, index);
        Assert::IsTrue(Read(test, textures[index], 0, 0) == NumberedTexel(index), where.c_str());
      }
      textures.clear();
      test.device.WaitIdle();
    }
    ExpectClean(test);
  }

  TEST_METHOD(RefusesWhatItCannotClear)
  {
    TestDevice test;
    Open(test);
    Texture color = Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 4, 4, 1, 1);
    Texture volume = Make(test, TextureDimension::Texture3D, TextureFormat::Rgba8, 4, 4, 4, 2);
    Texture depth = Make(test, TextureDimension::Texture2D, TextureFormat::Depth32F, 4, 4, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    context.ClearColor(color, 0, 1, UNORM_COLOR);  // a 2D texture has one layer
    context.ClearColor(color, 1, 0, UNORM_COLOR);  // and this one, one mip
    context.ClearColor(volume, 1, 2, UNORM_COLOR); // mip 1 of a 3D texture 4 deep is 2 deep
    context.ClearColor(depth, 0, 0, UNORM_COLOR);  // depth is cleared as depth
    context.ClearDepth(color, 1.0f);               // and colour as colour
    context.ClearDepth(depth, 1.5f);               // to a depth from 0 to 1
    const Texture mipmappedDepth = test.device.CreateTexture({.dimension = TextureDimension::Texture2D,
                                                              .format = TextureFormat::Depth32F,
                                                              .widthPixels = 4,
                                                              .heightPixels = 4,
                                                              .depthPixels = 1,
                                                              .mipLevels = 2,
                                                              .name = "TargetClears"});
    Assert::IsFalse(static_cast<bool>(mipmappedDepth), L"a depth texture has one mip level");
    Assert::AreEqual(std::size_t{7}, test.failures.size(), L"not every refusal was reported");
    test.failures.clear();
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
