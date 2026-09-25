// Tests/NeuronClientTests/TextureTransfers.cpp
//
// What DrawContext uploads to a texture or buffer, it reads back unchanged: in every colour format,
// in every mip of odd sizes, in every face of a cube and every slice of a 3D texture, through the
// upload ring and around it. The barriers it inserts raise nothing in the debug layer, and a texture
// goes only once the GPU is done with it (Design/ADR/ADR-007; plan Phase 3).
#include "pch.h"

#include "Buffer.h"
#include "Check.h"
#include "DrawContext.h"
#include "GraphicsDevice.h"
#include "TestDevice.h"
#include "Texture.h"

#include <algorithm>
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

/// Pages small enough that a few updates fill one, and a large one does not fit.
constexpr std::uint32_t SMALL_PAGE_BYTES = 64u << 10;

/// Texels for _texels texels of _format, different for each _seed. Floating-point formats get
/// ordinary numbers, so that no NaN or denormal can be changed on the way.
std::vector<std::byte> Pattern(TextureFormat _format, std::size_t _texels, std::uint32_t _seed)
{
  std::vector<std::byte> bytes(_texels * Neuron::TexelBytes(_format));
  switch (_format)
  {
  case TextureFormat::R16F:
  case TextureFormat::Rgba16F:
    // Halves from 1 to 2: exponent 15, the mantissa counting.
    for (std::size_t index = 0; index < bytes.size() / 2; ++index)
    {
      const auto half = static_cast<std::uint16_t>(0x3C00u | (((index * 7u) + _seed) & 0x3FFu));
      std::memcpy(&bytes[index * 2], &half, sizeof(half));
    }
    break;
  case TextureFormat::R32F:
  case TextureFormat::Rgba32F:
    for (std::size_t index = 0; index < bytes.size() / 4; ++index)
    {
      const float value = (static_cast<float>(index) * 0.25f) + static_cast<float>(_seed);
      std::memcpy(&bytes[index * 4], &value, sizeof(value));
    }
    break;
  default:
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
      bytes[index] = static_cast<std::byte>(((index * 37u) + (std::size_t{_seed} * 101u) + 11u) & 0xFFu);
    }
    break;
  }
  return bytes;
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
                                                .name = "TextureTransfers"});
  Assert::IsTrue(static_cast<bool>(texture), L"the texture was not made");
  return texture;
}

/// Fills every level of every face with its own pattern, then reads each back.
void ExpectRoundTrip(TestDevice& _test, Texture& _texture, std::uint32_t _seed)
{
  Neuron::DrawContext& context = _test.device.Context();
  for (std::uint32_t face = 0; face < _texture.Faces(); ++face)
  {
    for (std::uint32_t mip = 0; mip < _texture.MipLevels(); ++mip)
    {
      context.UpdateTexture(_texture, mip, face, Pattern(_texture.Format(), LevelTexels(_texture, mip), _seed + (face * 16) + mip));
    }
  }
  for (std::uint32_t face = 0; face < _texture.Faces(); ++face)
  {
    for (std::uint32_t mip = 0; mip < _texture.MipLevels(); ++mip)
    {
      std::vector<std::byte> texels;
      Assert::IsTrue(context.ReadTexture(_texture, mip, face, texels), L"ReadTexture failed");
      const std::wstring where = std::format(L"format {}, face {}, mip {}", static_cast<int>(_texture.Format()), face, mip);
      Assert::IsTrue(texels == Pattern(_texture.Format(), LevelTexels(_texture, mip), _seed + (face * 16) + mip), where.c_str());
    }
  }
}

} // namespace

TEST_CLASS(TextureTransfers)
{
public:
  TEST_METHOD(RoundTripsEveryColorFormat)
  {
    TestDevice test;
    Open(test);
    for (const TextureFormat format : COLOR_FORMATS)
    {
      Texture texture = Make(test, TextureDimension::Texture2D, format, 5, 3, 1, 1);
      ExpectRoundTrip(test, texture, static_cast<std::uint32_t>(format));
    }
    ExpectClean(test);
  }

  TEST_METHOD(RoundTripsEachMipOfAnOddSizedTexture)
  {
    TestDevice test;
    Open(test);
    Texture texture = Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 7, 5, 1, Texture::FullMipLevels(7, 5, 1));
    Assert::AreEqual(3u, texture.MipLevels());
    Assert::AreEqual(1u, texture.WidthPixels(2));
    ExpectRoundTrip(test, texture, 1);
    ExpectClean(test);
  }

  TEST_METHOD(RoundTripsEachFaceOfACube)
  {
    TestDevice test;
    Open(test);
    Texture texture = Make(test, TextureDimension::TextureCube, TextureFormat::Rgba16F, 4, 4, 1, 2);
    Assert::AreEqual(6u, texture.Faces());
    ExpectRoundTrip(test, texture, 2);
    ExpectClean(test);
  }

  TEST_METHOD(RoundTripsA3DTexture)
  {
    TestDevice test;
    Open(test);
    Texture texture = Make(test, TextureDimension::Texture3D, TextureFormat::R32F, 4, 3, 5, 2);
    Assert::AreEqual(2u, texture.DepthPixels(1));
    ExpectRoundTrip(test, texture, 3);
    ExpectClean(test);
  }

  TEST_METHOD(KeepsTheLastOfTwoUpdatesInOneFrame)
  {
    TestDevice test;
    Open(test);
    Texture texture = Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 16, 16, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    context.UpdateTexture(texture, 0, 0, Pattern(TextureFormat::Rgba8, 256, 4));
    context.UpdateTexture(texture, 0, 0, Pattern(TextureFormat::Rgba8, 256, 5));
    std::vector<std::byte> texels;
    Assert::IsTrue(context.ReadTexture(texture, 0, 0, texels));
    Assert::IsTrue(texels == Pattern(TextureFormat::Rgba8, 256, 5), L"the second update did not win");
    ExpectClean(test);
  }

  TEST_METHOD(StagesAnUploadLargerThanAPage)
  {
    TestDevice test;
    Open(test, SMALL_PAGE_BYTES);
    // 256 KiB, four pages' worth.
    Texture texture = Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 256, 256, 1, 1);
    ExpectRoundTrip(test, texture, 6);
    ExpectClean(test);
  }

  TEST_METHOD(ReusesUploadPagesAcrossFrames)
  {
    TestDevice test;
    Open(test, SMALL_PAGE_BYTES);
    // 40 KiB each, so that every update takes a page of its own.
    Texture texture = Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 128, 80, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    std::uint32_t seed = 0;
    for (int frame = 0; frame < 6; ++frame)
    {
      test.device.BeginFrame();
      for (int update = 0; update < 3; ++update)
      {
        context.UpdateTexture(texture, 0, 0, Pattern(TextureFormat::Rgba8, LevelTexels(texture, 0), ++seed));
      }
      test.device.EndFrame();
    }
    std::vector<std::byte> texels;
    Assert::IsTrue(context.ReadTexture(texture, 0, 0, texels));
    Assert::IsTrue(texels == Pattern(TextureFormat::Rgba8, LevelTexels(texture, 0), seed), L"the last update did not arrive");
    ExpectClean(test);
  }

  TEST_METHOD(RoundTripsABuffer)
  {
    TestDevice test;
    Open(test);
    Neuron::Buffer buffer = test.device.CreateBuffer({.sizeBytes = 1000, .strideBytes = 0, .name = "TextureTransfers"});
    Assert::IsTrue(static_cast<bool>(buffer));
    const std::vector<std::byte> head = Pattern(TextureFormat::R8, 600, 7);
    const std::vector<std::byte> tail = Pattern(TextureFormat::R8, 400, 8);
    Neuron::DrawContext& context = test.device.Context();
    context.UpdateBuffer(buffer, 0, head);
    context.UpdateBuffer(buffer, 600, tail);
    std::vector<std::byte> bytes;
    Assert::IsTrue(context.ReadBuffer(buffer, bytes));
    std::vector<std::byte> expected = head;
    expected.insert(expected.end(), tail.begin(), tail.end());
    Assert::IsTrue(bytes == expected, L"the buffer did not read back as written");
    // A later command list finds it decayed to COMMON, and the barriers still agree.
    context.UpdateBuffer(buffer, 0, tail);
    Assert::IsTrue(context.ReadBuffer(buffer, bytes));
    Assert::IsTrue(std::equal(tail.begin(), tail.end(), bytes.begin()), L"the second write did not arrive");
    ExpectClean(test);
  }

  TEST_METHOD(ReleasesATextureOnceTheGpuIsDone)
  {
    TestDevice test;
    Open(test);
    test.device.BeginFrame();
    {
      Texture texture = Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 8, 8, 1, 1);
      test.device.Context().UpdateTexture(texture, 0, 0, Pattern(TextureFormat::Rgba8, 64, 9));
    }
    // Its copy is recorded and not yet submitted, so it waits.
    Assert::AreEqual(std::size_t{1}, test.device.PendingReleases());
    test.device.EndFrame();
    test.device.BeginFrame();
    test.device.EndFrame();
    // The third frame waits for the first.
    test.device.BeginFrame();
    Assert::AreEqual(std::size_t{0}, test.device.PendingReleases());
    test.device.EndFrame();
    ExpectClean(test);
  }

  TEST_METHOD(ReadsBackWithoutWaiting)
  {
    TestDevice test;
    Open(test);
    Texture texture = Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 4, 4, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    context.UpdateTexture(texture, 0, 0, Pattern(TextureFormat::Rgba8, 16, 11));
    const std::uint64_t ticket = context.RequestRead(texture, 0, 0);
    Assert::AreNotEqual(std::uint64_t{0}, ticket, L"the read did not start");
    std::vector<std::byte> texels;
    Assert::IsFalse(context.TakeRead(ticket, texels), L"the read was taken before the GPU could have done it");
    test.device.WaitIdle();
    Assert::IsTrue(context.TakeRead(ticket, texels), L"the read was not there once the GPU was idle");
    Assert::IsTrue(texels == Pattern(TextureFormat::Rgba8, 16, 11), L"the read did not give the texels");
    // A ticket is spent once taken.
    Assert::IsFalse(context.TakeRead(ticket, texels));
    Assert::AreEqual(std::size_t{1}, test.failures.size(), L"a spent ticket was not reported");
    test.failures.clear();
    ExpectClean(test);
  }

  TEST_METHOD(ReadsBackFramesLater)
  {
    TestDevice test;
    Open(test);
    Texture texture = Make(test, TextureDimension::Texture2D, TextureFormat::R32F, 3, 2, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    // As the lens flares read their visibility: asked for in one frame, taken in a later one.
    test.device.BeginFrame();
    context.UpdateTexture(texture, 0, 0, Pattern(TextureFormat::R32F, 6, 12));
    const std::uint64_t ticket = context.RequestRead(texture, 0, 0);
    test.device.EndFrame();
    for (std::uint64_t frame = 1; frame < Neuron::GraphicsDevice::FRAMES_IN_FLIGHT; ++frame)
    {
      test.device.BeginFrame();
      test.device.EndFrame();
    }
    // Beginning this frame waits for the one FRAMES_IN_FLIGHT before it, which asked for the read.
    test.device.BeginFrame();
    std::vector<std::byte> texels;
    Assert::IsTrue(context.TakeRead(ticket, texels), L"the read was not there frames later");
    Assert::IsTrue(texels == Pattern(TextureFormat::R32F, 6, 12), L"the read did not give the texels");
    test.device.EndFrame();
    ExpectClean(test);
  }

  TEST_METHOD(RefusesAnUpdateOfTheWrongSize)
  {
    TestDevice test;
    Open(test);
    Texture texture = Make(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 4, 4, 1, 1);
    test.device.Context().UpdateTexture(texture, 0, 0, Pattern(TextureFormat::Rgba8, 15, 10));
    Assert::AreEqual(std::size_t{1}, test.failures.size(), L"the short update was not reported");
    Assert::IsTrue(test.failures.front().find("64") != std::string::npos, Widen(test.failures.front()).c_str());
    test.failures.clear();
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
