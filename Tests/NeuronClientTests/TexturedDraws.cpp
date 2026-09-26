// Tests/NeuronClientTests/TexturedDraws.cpp
//
// Draws sample textures through the samplers liblt describes: a 2D texture with its row 0 at the
// bottom, as GL had it; each wrap mode, the border colour and linear filtering; depth; a cube's
// faces and a 3D texture's slices. The shader-visible tables they go through wrap around their
// ring, a draw that reads none leaves the last table to the next draw that reads its textures, and
// the sampler heap starts again when it fills, with no debug-layer error. A draw may not sample
// what it draws into (Design/ADR/ADR-007; plan §5.3 and Phase 3).
#include "pch.h"

#include "Check.h"
#include "CompiledShaders/CubePS.h"
#include "CompiledShaders/SolidPS.h"
#include "CompiledShaders/SolidVS.h"
#include "CompiledShaders/TexturedPS.h"
#include "CompiledShaders/TexturedVS.h"
#include "CompiledShaders/VolumePS.h"
#include "DrawContext.h"
#include "GraphicsDevice.h"
#include "Program.h"
#include "TestDevice.h"
#include "Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Neuron::BlendMode;
using Neuron::ColorTarget;
using Neuron::CullMode;
using Neuron::IndexFormat;
using Neuron::MipFilter;
using Neuron::Program;
using Neuron::RenderState;
using Neuron::SamplerDesc;
using Neuron::Texture;
using Neuron::TextureDimension;
using Neuron::TextureFilter;
using Neuron::TextureFormat;
using Neuron::TextureWrap;
using Neuron::VertexAttribute;
using Neuron::VertexFormat;
using Neuron::VertexLayout;

constexpr std::array<VertexAttribute, 2> TEXTURED_ATTRIBUTES = {
  {{"POSITION", 0, VertexFormat::Float3, 0}, {"TEXCOORD", 0, VertexFormat::Float2, 3 * sizeof(float)}}};
constexpr VertexLayout TEXTURED{TEXTURED_ATTRIBUTES, 5 * sizeof(float)};

constexpr std::array<std::uint16_t, 6> QUAD_INDICES = {0, 1, 2, 0, 2, 3};

constexpr RenderState PLAIN{
  .blend = BlendMode::Opaque, .cull = CullMode::None, .depthTest = false, .depthWrite = false, .wireframe = false};
constexpr RenderState ADDITIVE{
  .blend = BlendMode::Additive, .cull = CullMode::None, .depthTest = false, .depthWrite = false, .wireframe = false};

/// The four texels of the wrap texture: a linear sample halfway between the second and third is
/// 102, which is exact.
constexpr std::array<std::uint8_t, 4> WRAP_TEXELS = {0, 51, 153, 204};

/// FXC's header holds the bytecode as an array of bytes.
template <typename T, std::size_t Count> std::span<const std::byte> Bytecode(const T (&_bytecode)[Count])
{
  return std::as_bytes(std::span(_bytecode));
}

template <typename T> std::span<const std::byte> Bytes(const T& _value)
{
  return std::as_bytes(std::span(&_value, 1));
}

/// A quad over the whole of clip space, counter-clockwise from the bottom left, with (_u0, _v0) at
/// the bottom left and (_u1, _v1) at the top right.
std::array<float, 20> Quad(float _u0, float _v0, float _u1, float _v1)
{
  return {-1.0f, -1.0f, 0.0f, _u0, _v0, 1.0f, -1.0f, 0.0f, _u1, _v0, 1.0f, 1.0f, 0.0f, _u1, _v1, -1.0f, 1.0f, 0.0f, _u0, _v1};
}

SamplerDesc Sampler(TextureFilter _filter, TextureWrap _wrap, const std::array<float, 4>& _border = {})
{
  return {.magFilter = _filter,
          .minFilter = _filter,
          .mipFilter = MipFilter::None,
          .wrapU = _wrap,
          .wrapV = _wrap,
          .wrapW = _wrap,
          .lodBias = 0.0f,
          .minLod = -1000.0f,
          .maxLod = 1000.0f,
          .maxAnisotropy = 1,
          .borderColor = _border};
}

Program MakeProgram(TestDevice& _test, std::span<const std::byte> _pixelShader, std::string_view _name)
{
  Program program =
    _test.device.CreateProgram({.vertexShader = Bytecode(TEXTURED_VS), .pixelShader = _pixelShader, .computeShader = {}, .name = _name});
  Assert::IsTrue(static_cast<bool>(program), L"the program was not made");
  return program;
}

Texture MakeTexture(TestDevice& _test, TextureDimension _dimension, TextureFormat _format, std::uint32_t _width, std::uint32_t _height,
                    std::uint32_t _depth = 1)
{
  Texture texture = _test.device.CreateTexture({.dimension = _dimension,
                                                .format = _format,
                                                .widthPixels = _width,
                                                .heightPixels = _height,
                                                .depthPixels = _depth,
                                                .mipLevels = 1,
                                                .name = "TexturedDraws"});
  Assert::IsTrue(static_cast<bool>(texture), L"the texture was not made");
  return texture;
}

void SetTarget(TestDevice& _test, Texture& _target)
{
  const std::array<ColorTarget, 1> targets = {{{&_target, 0, 0}}};
  _test.device.Context().SetTargets(targets, nullptr);
}

std::vector<std::byte> Read(TestDevice& _test, const Texture& _texture)
{
  std::vector<std::byte> texels;
  Assert::IsTrue(_test.device.Context().ReadTexture(_texture, 0, 0, texels), L"ReadTexture failed");
  return texels;
}

/// Draws a quad over the whole target that samples at (_u, _v) everywhere.
void DrawAt(TestDevice& _test, float _u, float _v)
{
  _test.device.Context().DrawTransient(Bytes(Quad(_u, _v, _u, _v)), TEXTURED, Bytes(QUAD_INDICES), IndexFormat::UInt16);
}

std::vector<std::byte> Gray(std::uint8_t _value, std::size_t _texels)
{
  return std::vector<std::byte>(_texels, static_cast<std::byte>(_value));
}

/// _count copies of _value.
std::vector<std::byte> Repeated(float _value, std::size_t _count)
{
  std::vector<std::byte> bytes;
  for (std::size_t index = 0; index < _count; ++index)
  {
    const std::span<const std::byte> value = Bytes(_value);
    bytes.insert(bytes.end(), value.begin(), value.end());
  }
  return bytes;
}

} // namespace

TEST_CLASS(TexturedDraws)
{
public:
  TEST_METHOD(SamplesRowZeroAtTheBottom)
  {
    TestDevice test;
    Open(test);
    Program textured = MakeProgram(test, Bytecode(TEXTURED_PS), "Textured");
    Texture image = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 2, 2);
    Texture target = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 2, 2);
    Neuron::DrawContext& context = test.device.Context();
    std::vector<std::byte> texels(16);
    for (std::size_t index = 0; index < texels.size(); ++index)
    {
      texels[index] = static_cast<std::byte>((index * 13u) + 7u);
    }
    context.UpdateTexture(image, 0, 0, texels);
    SetTarget(test, target);
    context.SetState(PLAIN);
    context.SetProgram(textured);
    context.SetTexture(0, &image);
    context.SetSampler(0, Sampler(TextureFilter::Nearest, TextureWrap::ClampToEdge));
    context.DrawTransient(Bytes(Quad(0.0f, 0.0f, 1.0f, 1.0f)), TEXTURED, Bytes(QUAD_INDICES), IndexFormat::UInt16);
    Assert::IsTrue(Read(test, target) == texels, L"the image was not drawn as it is stored, row 0 at the bottom");
    ExpectClean(test);
  }

  TEST_METHOD(WrapsAndFiltersAsTheSamplerSays)
  {
    TestDevice test;
    Open(test);
    Program textured = MakeProgram(test, Bytecode(TEXTURED_PS), "Textured");
    Texture image = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R8, 4, 1);
    Texture target = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R8, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    context.UpdateTexture(image, 0, 0, std::as_bytes(std::span(WRAP_TEXELS)));
    SetTarget(test, target);
    context.SetState(PLAIN);
    context.SetProgram(textured);
    context.SetTexture(0, &image);
    struct Case
    {
      SamplerDesc sampler;
      float u;
      std::uint8_t expected;
    };
    // 1.375 is the second texel repeated, the third mirrored, and the last clamped. The SDF field's
    // border is red.
    const std::array<Case, 5> cases = {{
      {Sampler(TextureFilter::Nearest, TextureWrap::Repeat), 1.375f, WRAP_TEXELS[1]},
      {Sampler(TextureFilter::Nearest, TextureWrap::MirroredRepeat), 1.375f, WRAP_TEXELS[2]},
      {Sampler(TextureFilter::Nearest, TextureWrap::ClampToEdge), 1.375f, WRAP_TEXELS[3]},
      {Sampler(TextureFilter::Nearest, TextureWrap::ClampToBorder, {1.0f, 0.0f, 0.0f, 0.0f}), 1.375f, 0xFF},
      {Sampler(TextureFilter::Linear, TextureWrap::ClampToEdge), 0.5f, 102},
    }};
    for (std::size_t index = 0; index < cases.size(); ++index)
    {
      context.SetSampler(0, cases[index].sampler);
      DrawAt(test, cases[index].u, 0.5f);
      const std::wstring where = std::format(L"case {}", index);
      Assert::IsTrue(Read(test, target) == Gray(cases[index].expected, 1), where.c_str());
    }
    ExpectClean(test);
  }

  TEST_METHOD(SamplesDepth)
  {
    TestDevice test;
    Open(test);
    Program textured = MakeProgram(test, Bytecode(TEXTURED_PS), "Textured");
    Texture depth = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::Depth32F, 2, 2);
    Texture target = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R32F, 2, 2);
    Neuron::DrawContext& context = test.device.Context();
    context.ClearDepth(depth, 0.25f);
    SetTarget(test, target);
    context.SetState(PLAIN);
    context.SetProgram(textured);
    context.SetTexture(0, &depth);
    context.SetSampler(0, Sampler(TextureFilter::Nearest, TextureWrap::ClampToEdge));
    context.DrawTransient(Bytes(Quad(0.0f, 0.0f, 1.0f, 1.0f)), TEXTURED, Bytes(QUAD_INDICES), IndexFormat::UInt16);
    Assert::IsTrue(Read(test, target) == Repeated(0.25f, 4), L"depth did not sample as it was cleared");
    ExpectClean(test);
  }

  TEST_METHOD(SamplesACubeAndA3DTexture)
  {
    TestDevice test;
    Open(test);
    Program cubeProgram = MakeProgram(test, Bytecode(CUBE_PS), "Cube");
    Program volumeProgram = MakeProgram(test, Bytecode(VOLUME_PS), "Volume");
    Texture cube = MakeTexture(test, TextureDimension::TextureCube, TextureFormat::R8, 2, 2);
    Texture volume = MakeTexture(test, TextureDimension::Texture3D, TextureFormat::R8, 2, 2, 4);
    Texture target = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R8, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    // Face f, and slice s, are cleared to 40 (f + 1) and 50 (s + 1).
    for (std::uint32_t face = 0; face < 6; ++face)
    {
      const float gray = static_cast<float>(40 * (face + 1)) / 255.0f;
      context.ClearColor(cube, 0, face, {gray, gray, gray, gray});
    }
    for (std::uint32_t slice = 0; slice < 4; ++slice)
    {
      const float gray = static_cast<float>(50 * (slice + 1)) / 255.0f;
      context.ClearColor(volume, 0, slice, {gray, gray, gray, gray});
    }
    SetTarget(test, target);
    context.SetState(PLAIN);
    context.SetSampler(0, Sampler(TextureFilter::Nearest, TextureWrap::ClampToEdge));

    // +X, -X, +Y, -Y, +Z and -Z, the faces' order in both GL and Direct3D.
    const std::array<std::array<float, 3>, 6> directions = {
      {{1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}};
    context.SetProgram(cubeProgram);
    context.SetTexture(0, &cube);
    for (std::uint32_t face = 0; face < 6; ++face)
    {
      cubeProgram.SetConstant("direction", Bytes(directions[face]));
      DrawAt(test, 0.5f, 0.5f);
      const std::wstring where = std::format(L"face {}", face);
      Assert::IsTrue(Read(test, target) == Gray(static_cast<std::uint8_t>(40 * (face + 1)), 1), where.c_str());
    }
    context.SetProgram(volumeProgram);
    context.SetTexture(0, &volume);
    for (std::uint32_t slice = 0; slice < 4; ++slice)
    {
      const std::array<float, 3> coordinate = {0.5f, 0.5f, (static_cast<float>(slice) + 0.5f) / 4.0f};
      volumeProgram.SetConstant("coordinate", Bytes(coordinate));
      DrawAt(test, 0.5f, 0.5f);
      const std::wstring where = std::format(L"slice {}", slice);
      Assert::IsTrue(Read(test, target) == Gray(static_cast<std::uint8_t>(50 * (slice + 1)), 1), where.c_str());
    }
    ExpectClean(test);
  }

  TEST_METHOD(WrapsTheDescriptorRing)
  {
    TestDevice test;
    // The null table and three tables of sixteen: every draw below takes a table, eight a frame.
    Open(test, Neuron::GraphicsDevice::DEFAULT_UPLOAD_PAGE_BYTES, 16 + (3 * 16));
    Program textured = MakeProgram(test, Bytecode(TEXTURED_PS), "Textured");
    Texture one = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R32F, 1, 1);
    Texture two = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R32F, 1, 1);
    Texture target = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R32F, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    context.UpdateTexture(one, 0, 0, Bytes(1.0f));
    context.UpdateTexture(two, 0, 0, Bytes(2.0f));
    context.ClearColor(target, 0, 0, {0.0f, 0.0f, 0.0f, 0.0f});
    SetTarget(test, target);
    context.SetState(ADDITIVE);
    context.SetProgram(textured);
    context.SetSampler(0, Sampler(TextureFilter::Nearest, TextureWrap::ClampToEdge));
    for (int frame = 0; frame < 4; ++frame)
    {
      test.device.BeginFrame();
      for (int draw = 0; draw < 8; ++draw)
      {
        context.SetTexture(0, draw % 2 == 0 ? &one : &two);
        DrawAt(test, 0.5f, 0.5f);
      }
      test.device.EndFrame();
    }
    // Four frames of four ones and four twos.
    Assert::IsTrue(Read(test, target) == Repeated(48.0f, 1), L"a draw read the wrong table");
    ExpectClean(test);
  }

  TEST_METHOD(ReadsItsTexturesAfterADrawThatReadsNone)
  {
    TestDevice test;
    Open(test);
    Program textured = MakeProgram(test, Bytecode(TEXTURED_PS), "Textured");
    Program solid = test.device.CreateProgram(
      {.vertexShader = Bytecode(SOLID_VS), .pixelShader = Bytecode(SOLID_PS), .computeShader = {}, .name = "Solid"});
    Assert::IsTrue(static_cast<bool>(solid), L"the program was not made");
    Texture image = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R32F, 1, 1);
    Texture target = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R32F, 1, 1);
    Neuron::DrawContext& context = test.device.Context();
    context.UpdateTexture(image, 0, 0, Bytes(1.0f));
    context.ClearColor(target, 0, 0, {0.0f, 0.0f, 0.0f, 0.0f});
    SetTarget(test, target);
    context.SetState(ADDITIVE);
    context.SetSampler(0, Sampler(TextureFilter::Nearest, TextureWrap::ClampToEdge));
    context.SetTexture(0, &image);
    // Text over panels, as liblt's interface draws it: one texture read twice in one list, with a
    // draw between that reads none. The solid program's colour is left at 0, so it adds nothing.
    context.SetProgram(textured);
    DrawAt(test, 0.5f, 0.5f);
    context.SetProgram(solid);
    DrawAt(test, 0.5f, 0.5f);
    context.SetProgram(textured);
    DrawAt(test, 0.5f, 0.5f);
    Assert::IsTrue(Read(test, target) == Repeated(2.0f, 1), L"the second textured draw read the null table");
    ExpectClean(test);
  }

  TEST_METHOD(StartsTheSamplerHeapAgainWhenItFills)
  {
    TestDevice test;
    Open(test);
    Program textured = MakeProgram(test, Bytecode(TEXTURED_PS), "Textured");
    Texture image = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R8, 4, 1);
    Neuron::DrawContext& context = test.device.Context();
    context.UpdateTexture(image, 0, 0, std::as_bytes(std::span(WRAP_TEXELS)));
    context.SetState(PLAIN);
    context.SetProgram(textured);
    context.SetTexture(0, &image);
    // More distinct sampler tables than the heap's 2,048 samplers hold, each draw's border its own.
    constexpr std::uint32_t DRAWS = 130;
    std::vector<Texture> targets;
    for (std::uint32_t draw = 0; draw < DRAWS; ++draw)
    {
      targets.push_back(MakeTexture(test, TextureDimension::Texture2D, TextureFormat::R8, 1, 1));
      SetTarget(test, targets.back());
      const float red = static_cast<float>(draw + 1) / 255.0f;
      context.SetSampler(0, Sampler(TextureFilter::Nearest, TextureWrap::ClampToBorder, {red, 0.0f, 0.0f, 0.0f}));
      DrawAt(test, 1.375f, 0.5f);
    }
    for (std::uint32_t draw = 0; draw < DRAWS; ++draw)
    {
      const std::wstring where = std::format(L"draw {}", draw);
      Assert::IsTrue(Read(test, targets[draw]) == Gray(static_cast<std::uint8_t>(draw + 1), 1), where.c_str());
    }
    ExpectClean(test);
  }

  TEST_METHOD(RefusesToSampleWhatItDrawsInto)
  {
    TestDevice test;
    Open(test);
    Program textured = MakeProgram(test, Bytecode(TEXTURED_PS), "Textured");
    Texture target = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::Rgba8, 2, 2);
    Neuron::DrawContext& context = test.device.Context();
    SetTarget(test, target);
    context.SetState(PLAIN);
    context.SetProgram(textured);
    context.SetSampler(0, Sampler(TextureFilter::Nearest, TextureWrap::ClampToEdge));

    // A slot the program does not read may hold it, as a GL texture unit could.
    context.SetTexture(5, &target);
    DrawAt(test, 0.5f, 0.5f);
    Assert::IsTrue(test.failures.empty(), L"a texture in a slot the program does not read was refused");

    context.SetTexture(0, &target);
    DrawAt(test, 0.5f, 0.5f);
    context.SetTexture(Neuron::Program::MAX_SHADER_RESOURCES, &target);
    context.SetSampler(Neuron::Program::MAX_SAMPLERS, Sampler(TextureFilter::Nearest, TextureWrap::ClampToEdge));
    Assert::AreEqual(std::size_t{3}, test.failures.size(), L"not every refusal was reported");
    test.failures.clear();
    test.device.WaitIdle();
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
