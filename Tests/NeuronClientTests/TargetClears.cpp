// Tests/NeuronClientTests/TargetClears.cpp
//
// DrawContext clears a colour texture's mip, cube face or 3D slice, and nothing beside it, to the
// value its format stores, and clears depth. It clears through views it makes on first use, which
// stay each texture's own until the texture goes, and it refuses what it cannot clear
// (Design/ADR/ADR-007; plan Phase 3).
#include "pch.h"

#include "Check.h"
#include "DrawContext.h"
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

constexpr std::array<TextureFormat, 7> COLOR_FORMATS = {TextureFormat::R8,     TextureFormat::Rg8,     TextureFormat::Rgba8,
                                                        TextureFormat::R16F,   TextureFormat::Rgba16F, TextureFormat::R32F,
                                                        TextureFormat::Rgba32F};

/// 51, 102, 153 and 204 in a UNORM format: each k / 255, which rounds back to k exactly.
constexpr std::array<float, 4> UNORM_COLOR = {0.2f, 0.4f, 0.6f, 0.8f};
constexpr std::array<std::uint8_t, 4> UNORM_BYTES = {51, 102, 153, 204};

/// Exact in every float format, halves included, and outside [0, 1], which a float target keeps.
constexpr std::array<float, 4> FLOAT_COLOR = {0.25f, -0.5f, 2.0f, 1024.0f};
constexpr std::array<std::uint16_t, 4> FLOAT_HALVES = {0x3400, 0xB800, 0x4000, 0x6400};

constexpr std::array<float, 4> TRANSPARENT_BLACK = {0.0f, 0.0f, 0.0f, 0.0f};

/// More textures than a block of views holds.
constexpr std::size_t MANY_TEXTURES = 300;

bool IsFloat(TextureFormat _format) noexcept
{
  return _format == TextureFormat::R16F || _format == TextureFormat::Rgba16F || _format == TextureFormat::R32F ||
         _format == TextureFormat::Rgba32F;
}

/// A colour every channel of _format stores exactly.
const std::array<float, 4>& ExactColor(TextureFormat _format) noexcept
{
  return IsFloat(_format) ? FLOAT_COLOR : UNORM_COLOR;
}

/// _texels texels of _format as a clear to ExactColor leaves them, packed as ReadTexture gives them.
std::vector<std::byte> Cleared(TextureFormat _format, std::size_t _texels)
{
  const std::uint32_t texelBytes = Neuron::TexelBytes(_format);
  std::array<std::byte, 16> texel{};
  switch (_format)
  {
  case TextureFormat::R16F:
  case TextureFormat::Rgba16F:
    std::memcpy(texel.data(), FLOAT_HALVES.data(), texelBytes);
    break;
  case TextureFormat::R32F:
  case TextureFormat::Rgba32F:
    std::memcpy(texel.data(), FLOAT_COLOR.data(), texelBytes);
    break;
  default:
    std::memcpy(texel.data(), UNORM_BYTES.data(), texelBytes);
    break;
  }
  std::vector<std::byte> texels(_texels * texelBytes);
  for (std::size_t index = 0; index < _texels; ++index)
  {
    std::memcpy(&texels[index * texelBytes], texel.data(), texelBytes);
  }
  return texels;
}

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
      Assert::IsTrue(Read(test, texture, 0, 0) == Cleared(format, 15), where.c_str());
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
          face == 3 && mip == 1 ? Cleared(TextureFormat::Rgba8, texels) : std::vector<std::byte>(texels * 4);
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
