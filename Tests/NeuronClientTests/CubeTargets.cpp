// Tests/NeuronClientTests/CubeTargets.cpp
//
// Draws go into any mip of any face of a cube, and a face drawn into keeps GL's layout: its row 0
// is the bottom of what was drawn, and a direction up the cube, +y, samples it there, as it did in
// GL. GL and Direct3D lay out a cube's faces alike, and the clip-space macro keeps what is drawn
// as GL stored it, so liblt's cube cameras need no change (Design/ADR/ADR-007; plan §5.5 and
// Phase 3).
#include "pch.h"

#include "Check.h"
#include "CompiledShaders/CubePS.h"
#include "CompiledShaders/SolidPS.h"
#include "CompiledShaders/SolidVS.h"
#include "CompiledShaders/TexturedVS.h"
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
using Neuron::Texture;
using Neuron::TextureDimension;
using Neuron::TextureFilter;
using Neuron::TextureFormat;
using Neuron::TextureWrap;
using Neuron::VertexAttribute;
using Neuron::VertexFormat;
using Neuron::VertexLayout;

constexpr std::array<VertexAttribute, 1> POSITION_ATTRIBUTES = {{{"POSITION", 0, VertexFormat::Float3, 0}}};
constexpr VertexLayout POSITIONS{POSITION_ATTRIBUTES, 3 * sizeof(float)};
constexpr std::array<VertexAttribute, 2> TEXTURED_ATTRIBUTES = {
  {{"POSITION", 0, VertexFormat::Float3, 0}, {"TEXCOORD", 0, VertexFormat::Float2, 3 * sizeof(float)}}};
constexpr VertexLayout TEXTURED{TEXTURED_ATTRIBUTES, 5 * sizeof(float)};
constexpr std::array<std::uint16_t, 6> QUAD_INDICES = {0, 1, 2, 0, 2, 3};

constexpr RenderState PLAIN{
  .blend = BlendMode::Opaque, .cull = CullMode::None, .depthTest = false, .depthWrite = false, .wireframe = false};

/// FXC's header holds the bytecode as an array of bytes.
template <typename T, std::size_t Count> std::span<const std::byte> Bytecode(const T (&_bytecode)[Count])
{
  return std::as_bytes(std::span(_bytecode));
}

template <typename T> std::span<const std::byte> Bytes(const T& _value)
{
  return std::as_bytes(std::span(&_value, 1));
}

Program MakeProgram(TestDevice& _test, std::span<const std::byte> _vertexShader, std::span<const std::byte> _pixelShader,
                    std::string_view _name)
{
  Program program =
    _test.device.CreateProgram({.vertexShader = _vertexShader, .pixelShader = _pixelShader, .computeShader = {}, .name = _name});
  Assert::IsTrue(static_cast<bool>(program), L"the program was not made");
  return program;
}

Texture MakeCube(TestDevice& _test, TextureFormat _format, std::uint32_t _size, std::uint32_t _mips)
{
  Texture texture = _test.device.CreateTexture({.dimension = TextureDimension::TextureCube,
                                                .format = _format,
                                                .widthPixels = _size,
                                                .heightPixels = _size,
                                                .depthPixels = 1,
                                                .mipLevels = _mips,
                                                .name = "CubeTargets"});
  Assert::IsTrue(static_cast<bool>(texture), L"the cube was not made");
  return texture;
}

/// Draws a quad in _gray over clip space from _bottom to _top, as GL has y.
void DrawBand(TestDevice& _test, Program& _solid, float _bottom, float _top, float _gray)
{
  const std::array<float, 12> quad = {-1.0f, _bottom, 0.0f, 1.0f, _bottom, 0.0f, 1.0f, _top, 0.0f, -1.0f, _top, 0.0f};
  _solid.SetConstant("color", Bytes(std::array<float, 4>{_gray, _gray, _gray, _gray}));
  Neuron::DrawContext& context = _test.device.Context();
  context.SetProgram(_solid);
  context.DrawTransient(Bytes(quad), POSITIONS, Bytes(QUAD_INDICES), IndexFormat::UInt16);
}

std::vector<std::byte> Read(TestDevice& _test, const Texture& _texture, std::uint32_t _mip, std::uint32_t _face)
{
  std::vector<std::byte> texels;
  Assert::IsTrue(_test.device.Context().ReadTexture(_texture, _mip, _face, texels), L"ReadTexture failed");
  return texels;
}

std::vector<std::byte> Gray(std::uint8_t _value, std::size_t _texels)
{
  return std::vector<std::byte>(_texels, static_cast<std::byte>(_value));
}

} // namespace

TEST_CLASS(CubeTargets)
{
public:
  TEST_METHOD(DrawsIntoEachMipOfEachFace)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_VS), Bytecode(SOLID_PS), "Solid");
    Texture cube = MakeCube(test, TextureFormat::R8, 4, 2);
    Neuron::DrawContext& context = test.device.Context();
    context.SetState(PLAIN);
    // Face f's mip m is drawn in 20 (1 + f + 6 m).
    for (std::uint32_t mip = 0; mip < 2; ++mip)
    {
      for (std::uint32_t face = 0; face < 6; ++face)
      {
        const std::array<ColorTarget, 1> target = {{{&cube, mip, face}}};
        context.SetTargets(target, nullptr);
        DrawBand(test, solid, -1.0f, 1.0f, static_cast<float>(20 * (1 + face + (6 * mip))) / 255.0f);
      }
    }
    for (std::uint32_t mip = 0; mip < 2; ++mip)
    {
      for (std::uint32_t face = 0; face < 6; ++face)
      {
        const std::size_t texels = std::size_t{cube.WidthPixels(mip)} * cube.HeightPixels(mip);
        const std::wstring where = std::format(L"mip {} of face {}", mip, face);
        Assert::IsTrue(Read(test, cube, mip, face) == Gray(static_cast<std::uint8_t>(20 * (1 + face + (6 * mip))), texels), where.c_str());
      }
    }
    ExpectClean(test);
  }

  TEST_METHOD(KeepsGlsLayoutInAFaceDrawnInto)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_VS), Bytecode(SOLID_PS), "Solid");
    Program sampleCube = MakeProgram(test, Bytecode(TEXTURED_VS), Bytecode(CUBE_PS), "Cube");
    Texture cube = MakeCube(test, TextureFormat::R8, 4, 1);
    Texture target = test.device.CreateTexture({.dimension = TextureDimension::Texture2D,
                                                .format = TextureFormat::R8,
                                                .widthPixels = 1,
                                                .heightPixels = 1,
                                                .depthPixels = 1,
                                                .mipLevels = 1,
                                                .name = "CubeTargets"});
    Neuron::DrawContext& context = test.device.Context();
    for (std::uint32_t face = 0; face < 6; ++face)
    {
      context.ClearColor(cube, 0, face, {0.0f, 0.0f, 0.0f, 0.0f});
    }
    context.SetState(PLAIN);

    // The bottom half of clip space, drawn into +X, is its rows 0 and 1.
    const std::array<ColorTarget, 1> plusX = {{{&cube, 0, 0}}};
    context.SetTargets(plusX, nullptr);
    DrawBand(test, solid, -1.0f, 0.0f, 1.0f);
    std::vector<std::byte> rows = Gray(0xFF, 8);
    rows.resize(16, std::byte{0});
    Assert::IsTrue(Read(test, cube, 0, 0) == rows, L"the face was not drawn with row 0 at the bottom");

    // Up the cube, +y, finds those rows, as in GL; down it finds the rest.
    const std::array<ColorTarget, 1> single = {{{&target, 0, 0}}};
    context.SetTargets(single, nullptr);
    context.SetProgram(sampleCube);
    context.SetTexture(0, &cube);
    context.SetSampler(0, {.magFilter = TextureFilter::Nearest,
                           .minFilter = TextureFilter::Nearest,
                           .mipFilter = MipFilter::None,
                           .wrapU = TextureWrap::ClampToEdge,
                           .wrapV = TextureWrap::ClampToEdge,
                           .wrapW = TextureWrap::ClampToEdge,
                           .lodBias = 0.0f,
                           .minLod = 0.0f,
                           .maxLod = 0.0f,
                           .maxAnisotropy = 1,
                           .borderColor = {}});
    const std::array<float, 20> quad = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 0.0f, 0.0f, 0.0f,
                                        1.0f,  1.0f,  0.0f, 0.0f, 0.0f, -1.0f, 1.0f,  0.0f, 0.0f, 0.0f};
    const std::array<std::array<float, 3>, 2> directions = {{{1.0f, 0.5f, 0.0f}, {1.0f, -0.5f, 0.0f}}};
    const std::array<std::uint8_t, 2> expected = {0xFF, 0};
    for (std::size_t index = 0; index < directions.size(); ++index)
    {
      sampleCube.SetConstant("direction", Bytes(directions[index]));
      context.DrawTransient(Bytes(quad), TEXTURED, Bytes(QUAD_INDICES), IndexFormat::UInt16);
      const std::wstring where = std::format(L"direction {}", index);
      Assert::IsTrue(Read(test, target, 0, 0) == Gray(expected[index], 1), where.c_str());
    }
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
