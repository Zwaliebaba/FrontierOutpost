// Tests/NeuronClientTests/DrawCalls.cpp
//
// DrawContext draws as liblt's GL build did: into every colour format and into two targets at once,
// with row 0 at the bottom, the viewport and scissor from the bottom left, GL's front faces, its
// depth test and liblt's blend modes, from buffers or from the upload ring. It makes each pipeline
// state once, forgets what goes, and refuses what it cannot draw (Design/ADR/ADR-007; plan §5.5
// and Phase 3).
#include "pch.h"

#include "Buffer.h"
#include "Check.h"
#include "CompiledShaders/NamesCS.h"
#include "CompiledShaders/SolidPS.h"
#include "CompiledShaders/SolidVS.h"
#include "CompiledShaders/TwoTargetsPS.h"
#include "DrawContext.h"
#include "ExactColors.h"
#include "GraphicsDevice.h"
#include "Program.h"
#include "TestDevice.h"
#include "Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
using Neuron::DrawState;
using Neuron::IndexFormat;
using Neuron::Program;
using Neuron::Texture;
using Neuron::TextureDimension;
using Neuron::TextureFormat;
using Neuron::VertexAttribute;
using Neuron::VertexFormat;
using Neuron::VertexLayout;

constexpr std::array<VertexAttribute, 1> POSITION_ATTRIBUTES = {{{"POSITION", 0, VertexFormat::Float3, 0}}};
constexpr VertexLayout POSITIONS{POSITION_ATTRIBUTES, 3 * sizeof(float)};

constexpr DrawState PLAIN{.blend = BlendMode::Opaque, .cull = CullMode::None, .depthTest = false, .depthWrite = false, .wireframe = false};

/// A triangle over the whole of clip space, which the first indices wind counter-clockwise, as
/// GL's front faces are, and the second clockwise.
constexpr std::array<float, 9> WHOLE = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
constexpr std::array<std::uint16_t, 3> FRONT_FACING = {0, 1, 2};
constexpr std::array<std::uint16_t, 3> BACK_FACING = {0, 2, 1};

constexpr std::array<std::uint16_t, 6> QUAD_INDICES = {0, 1, 2, 0, 2, 3};

constexpr std::array<float, 4> WHITE = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr std::array<float, 4> TRANSPARENT_BLACK = {0.0f, 0.0f, 0.0f, 0.0f};

/// FXC's header holds the bytecode as an array of bytes.
template <typename T, std::size_t Count> std::span<const std::byte> Bytecode(const T (&_bytecode)[Count])
{
  return std::as_bytes(std::span(_bytecode));
}

template <typename T> std::span<const std::byte> Bytes(const T& _value)
{
  return std::as_bytes(std::span(&_value, 1));
}

/// A quad over clip space from (_left, _bottom) to (_right, _top) at depth _z, as GL has it, front
/// face on.
std::array<float, 12> Quad(float _left, float _bottom, float _right, float _top, float _z)
{
  return {_left, _bottom, _z, _right, _bottom, _z, _right, _top, _z, _left, _top, _z};
}

/// _count copies of _value.
std::vector<std::byte> Repeated(std::span<const std::byte> _value, std::size_t _count)
{
  std::vector<std::byte> bytes;
  for (std::size_t index = 0; index < _count; ++index)
  {
    bytes.insert(bytes.end(), _value.begin(), _value.end());
  }
  return bytes;
}

Program MakeProgram(TestDevice& _test, std::span<const std::byte> _pixelShader, std::string_view _name)
{
  Program program =
    _test.device.CreateProgram({.vertexShader = Bytecode(SOLID_VS), .pixelShader = _pixelShader, .computeShader = {}, .name = _name});
  Assert::IsTrue(static_cast<bool>(program), L"the program was not made");
  return program;
}

Texture MakeTexture(TestDevice& _test, TextureFormat _format, std::uint32_t _width, std::uint32_t _height)
{
  Texture texture = _test.device.CreateTexture({.dimension = TextureDimension::Texture2D,
                                                .format = _format,
                                                .widthPixels = _width,
                                                .heightPixels = _height,
                                                .depthPixels = 1,
                                                .mipLevels = 1,
                                                .name = "DrawCalls"});
  Assert::IsTrue(static_cast<bool>(texture), L"the texture was not made");
  return texture;
}

void SetTarget(TestDevice& _test, Texture& _target, Texture* _depth = nullptr)
{
  const std::array<ColorTarget, 1> targets = {{{&_target, 0, 0}}};
  _test.device.Context().SetTargets(targets, _depth);
}

std::vector<std::byte> Read(TestDevice& _test, const Texture& _texture)
{
  std::vector<std::byte> texels;
  Assert::IsTrue(_test.device.Context().ReadTexture(_texture, 0, 0, texels), L"ReadTexture failed");
  return texels;
}

/// Draws the triangle over the whole target in _color, wound as _indices say.
void DrawWhole(TestDevice& _test, Program& _program, const std::array<float, 4>& _color,
               std::span<const std::uint16_t> _indices = FRONT_FACING)
{
  _program.SetConstant("color", Bytes(_color));
  Neuron::DrawContext& context = _test.device.Context();
  context.SetProgram(_program);
  context.DrawTransient(Bytes(WHOLE), POSITIONS, std::as_bytes(_indices), IndexFormat::UInt16);
}

/// Draws a quad over the whole target at depth _z, in grey _gray.
void DrawAtDepth(TestDevice& _test, Program& _program, float _z, float _gray)
{
  _program.SetConstant("color", Bytes(std::array<float, 4>{_gray, _gray, _gray, _gray}));
  Neuron::DrawContext& context = _test.device.Context();
  context.SetProgram(_program);
  context.DrawTransient(Bytes(Quad(-1.0f, -1.0f, 1.0f, 1.0f, _z)), POSITIONS, Bytes(QUAD_INDICES), IndexFormat::UInt16);
}

std::vector<std::byte> Gray(std::uint8_t _value, std::size_t _texels)
{
  return std::vector<std::byte>(_texels, static_cast<std::byte>(_value));
}

} // namespace

TEST_CLASS(DrawCalls)
{
public:
  TEST_METHOD(DrawsIntoEveryColorFormat)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    for (const TextureFormat format : COLOR_FORMATS)
    {
      Texture target = MakeTexture(test, format, 5, 3);
      SetTarget(test, target);
      test.device.Context().SetState(PLAIN);
      DrawWhole(test, solid, ExactColor(format));
      const std::wstring where = std::format(L"format {}", static_cast<int>(format));
      Assert::IsTrue(Read(test, target) == ExactTexels(format, 15), where.c_str());
    }
    ExpectClean(test);
  }

  TEST_METHOD(KeepsRowZeroAtTheBottom)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    Texture target = MakeTexture(test, TextureFormat::R8, 4, 4);
    Neuron::DrawContext& context = test.device.Context();
    SetTarget(test, target);
    context.ClearColor(target, 0, 0, TRANSPARENT_BLACK);
    context.SetState(PLAIN);
    // GL's bottom half of clip space, which is rows 0 and 1.
    solid.SetConstant("color", Bytes(WHITE));
    context.SetProgram(solid);
    context.DrawTransient(Bytes(Quad(-1.0f, -1.0f, 1.0f, 0.0f, 0.0f)), POSITIONS, Bytes(QUAD_INDICES), IndexFormat::UInt16);
    std::vector<std::byte> expected = Gray(0xFF, 8);
    expected.resize(16, std::byte{0});
    Assert::IsTrue(Read(test, target) == expected, L"the bottom half is not rows 0 and 1");
    ExpectClean(test);
  }

  TEST_METHOD(PlacesTheViewportAndScissorFromTheBottomLeft)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    Texture target = MakeTexture(test, TextureFormat::R8, 4, 4);
    Neuron::DrawContext& context = test.device.Context();
    SetTarget(test, target);
    context.SetState(PLAIN);
    constexpr std::byte ON{0xFF};
    constexpr std::byte OFF{0};

    context.ClearColor(target, 0, 0, TRANSPARENT_BLACK);
    context.SetViewport(0, 0, 2, 2);
    DrawWhole(test, solid, WHITE);
    const std::vector<std::byte> viewport = {ON, ON, OFF, OFF, ON, ON, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF};
    Assert::IsTrue(Read(test, target) == viewport, L"the viewport is not the bottom-left 2 by 2");

    context.ClearColor(target, 0, 0, TRANSPARENT_BLACK);
    context.SetViewport(0, 0, 4, 4);
    context.SetScissor(1, 2, 2, 1);
    DrawWhole(test, solid, WHITE);
    const std::vector<std::byte> scissor = {OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, ON, ON, OFF, OFF, OFF, OFF, OFF};
    Assert::IsTrue(Read(test, target) == scissor, L"the scissor is not columns 1 and 2 of row 2");

    context.DisableScissor();
    DrawWhole(test, solid, WHITE);
    Assert::IsTrue(Read(test, target) == Gray(0xFF, 16), L"the scissor stayed on");
    ExpectClean(test);
  }

  TEST_METHOD(CullsTheFacesGlCulls)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    Texture target = MakeTexture(test, TextureFormat::R8, 4, 4);
    Neuron::DrawContext& context = test.device.Context();
    SetTarget(test, target);
    struct Case
    {
      CullMode cull;
      std::span<const std::uint16_t> indices;
      bool drawn;
    };
    const std::array<Case, 6> cases = {{{CullMode::None, FRONT_FACING, true},
                                        {CullMode::None, BACK_FACING, true},
                                        {CullMode::Back, FRONT_FACING, true},
                                        {CullMode::Back, BACK_FACING, false},
                                        {CullMode::Front, FRONT_FACING, false},
                                        {CullMode::Front, BACK_FACING, true}}};
    for (std::size_t index = 0; index < cases.size(); ++index)
    {
      const Case& each = cases[index];
      context.ClearColor(target, 0, 0, TRANSPARENT_BLACK);
      context.SetState({.blend = BlendMode::Opaque, .cull = each.cull, .depthTest = false, .depthWrite = false, .wireframe = false});
      DrawWhole(test, solid, WHITE, each.indices);
      const std::wstring where = std::format(L"case {}", index);
      Assert::IsTrue(Read(test, target) == Gray(each.drawn ? 0xFF : 0, 16), where.c_str());
    }
    ExpectClean(test);
  }

  TEST_METHOD(TestsDepthAsGlDoes)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    Texture color = MakeTexture(test, TextureFormat::R8, 4, 4);
    Texture depth = MakeTexture(test, TextureFormat::Depth32F, 4, 4);
    Neuron::DrawContext& context = test.device.Context();
    SetTarget(test, color, &depth);
    context.ClearColor(color, 0, 0, TRANSPARENT_BLACK);
    context.ClearDepth(depth, 1.0f);

    // GL's z of 0.5 is stored as 0.75, 0.75 as 0.875 and 0.25 as 0.625, and the nearer passes.
    context.SetState({.blend = BlendMode::Opaque, .cull = CullMode::None, .depthTest = true, .depthWrite = true, .wireframe = false});
    DrawAtDepth(test, solid, 0.5f, UNORM_COLOR[0]);
    DrawAtDepth(test, solid, 0.75f, UNORM_COLOR[1]);
    DrawAtDepth(test, solid, 0.25f, UNORM_COLOR[2]);
    Assert::IsTrue(Read(test, color) == Gray(UNORM_BYTES[2], 16), L"the nearest draw did not win");
    const float stored = 0.625f;
    Assert::IsTrue(Read(test, depth) == Repeated(Bytes(stored), 16), L"GL's z of 0.25 was not stored as 0.625");

    // Without writes it still tests; without the test it neither tests nor writes.
    context.SetState({.blend = BlendMode::Opaque, .cull = CullMode::None, .depthTest = true, .depthWrite = false, .wireframe = false});
    DrawAtDepth(test, solid, -0.5f, UNORM_COLOR[3]);
    Assert::IsTrue(Read(test, color) == Gray(UNORM_BYTES[3], 16), L"a nearer draw that does not write depth did not pass");
    context.SetState(PLAIN);
    DrawAtDepth(test, solid, 0.9f, UNORM_COLOR[0]);
    Assert::IsTrue(Read(test, color) == Gray(UNORM_BYTES[0], 16), L"a draw without the depth test did not pass");
    Assert::IsTrue(Read(test, depth) == Repeated(Bytes(stored), 16), L"depth changed without being written");
    ExpectClean(test);
  }

  TEST_METHOD(BlendsAsLibltDoes)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    Texture target = MakeTexture(test, TextureFormat::Rgba32F, 2, 2);
    Neuron::DrawContext& context = test.device.Context();
    SetTarget(test, target);
    const std::array<float, 4> below = {0.25f, 0.5f, 0.75f, 1.0f};
    const std::array<float, 4> source = {1.0f, 0.0f, 0.0f, 0.5f};
    struct Case
    {
      BlendMode blend;
      std::array<float, 4> expected;
    };
    // Alpha weighs colour by the source's alpha and sums alpha; Additive sums both.
    const std::array<Case, 3> cases = {{{BlendMode::Opaque, {1.0f, 0.0f, 0.0f, 0.5f}},
                                        {BlendMode::Alpha, {0.625f, 0.25f, 0.375f, 1.5f}},
                                        {BlendMode::Additive, {1.25f, 0.5f, 0.75f, 1.5f}}}};
    for (const Case& blended : cases)
    {
      context.ClearColor(target, 0, 0, below);
      context.SetState({.blend = blended.blend, .cull = CullMode::None, .depthTest = false, .depthWrite = false, .wireframe = false});
      DrawWhole(test, solid, source);
      const std::wstring where = std::format(L"blend mode {}", static_cast<int>(blended.blend));
      Assert::IsTrue(Read(test, target) == Repeated(Bytes(blended.expected), 4), where.c_str());
    }
    ExpectClean(test);
  }

  TEST_METHOD(MakesEachPipelineStateOnce)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    Texture target = MakeTexture(test, TextureFormat::Rgba8, 2, 2);
    Texture other = MakeTexture(test, TextureFormat::R8, 2, 2);
    Neuron::DrawContext& context = test.device.Context();
    SetTarget(test, target);
    context.SetState(PLAIN);
    for (int draw = 0; draw < 3; ++draw)
    {
      DrawWhole(test, solid, WHITE);
    }
    Assert::AreEqual(std::size_t{1}, test.device.PipelineStates(), L"one state was made more than once");
    context.SetState({.blend = BlendMode::Alpha, .cull = CullMode::None, .depthTest = false, .depthWrite = false, .wireframe = false});
    DrawWhole(test, solid, WHITE);
    context.SetState(PLAIN);
    DrawWhole(test, solid, WHITE);
    Assert::AreEqual(std::size_t{2}, test.device.PipelineStates(), L"a blend mode did not take a state of its own");
    SetTarget(test, other);
    DrawWhole(test, solid, WHITE);
    Assert::AreEqual(std::size_t{3}, test.device.PipelineStates(), L"a target format did not take a state of its own");
    test.device.WaitIdle();
    ExpectClean(test);
  }

  TEST_METHOD(DrawsIndexedFromBuffers)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    Texture target = MakeTexture(test, TextureFormat::R8, 4, 4);
    Neuron::DrawContext& context = test.device.Context();
    // The first triangle faces away, and the second, from index 3, faces the viewer.
    const std::array<std::uint16_t, 6> shortIndices = {0, 2, 1, 0, 1, 2};
    const std::array<std::uint32_t, 3> longIndices = {0, 1, 2};
    Neuron::Buffer vertices = test.device.CreateBuffer({.sizeBytes = sizeof(WHOLE), .strideBytes = 12, .name = "DrawCalls vertices"});
    Neuron::Buffer shortBuffer =
      test.device.CreateBuffer({.sizeBytes = sizeof(shortIndices), .strideBytes = 2, .name = "DrawCalls indices"});
    Neuron::Buffer longBuffer = test.device.CreateBuffer({.sizeBytes = sizeof(longIndices), .strideBytes = 4, .name = "DrawCalls indices"});
    context.UpdateBuffer(vertices, 0, Bytes(WHOLE));
    context.UpdateBuffer(shortBuffer, 0, Bytes(shortIndices));
    context.UpdateBuffer(longBuffer, 0, Bytes(longIndices));
    SetTarget(test, target);
    context.SetState({.blend = BlendMode::Opaque, .cull = CullMode::Back, .depthTest = false, .depthWrite = false, .wireframe = false});
    solid.SetConstant("color", Bytes(WHITE));
    context.SetProgram(solid);

    context.ClearColor(target, 0, 0, TRANSPARENT_BLACK);
    context.DrawIndexed(vertices, POSITIONS, shortBuffer, IndexFormat::UInt16, 0, 3);
    Assert::IsTrue(Read(test, target) == Gray(0, 16), L"the triangle that faces away was drawn");
    context.DrawIndexed(vertices, POSITIONS, shortBuffer, IndexFormat::UInt16, 3, 3);
    Assert::IsTrue(Read(test, target) == Gray(0xFF, 16), L"the triangle from index 3 was not drawn");

    context.ClearColor(target, 0, 0, TRANSPARENT_BLACK);
    context.DrawIndexed(vertices, POSITIONS, longBuffer, IndexFormat::UInt32, 0, 3);
    Assert::IsTrue(Read(test, target) == Gray(0xFF, 16), L"32-bit indices did not draw");
    ExpectClean(test);
  }

  TEST_METHOD(DrawsIntoTwoTargets)
  {
    TestDevice test;
    Open(test);
    Program twoTargets = MakeProgram(test, Bytecode(TWO_TARGETS_PS), "TwoTargets");
    Texture first = MakeTexture(test, TextureFormat::Rgba8, 3, 2);
    Texture second = MakeTexture(test, TextureFormat::Rgba8, 3, 2);
    Neuron::DrawContext& context = test.device.Context();
    const std::array<ColorTarget, 2> targets = {{{&first, 0, 0}, {&second, 0, 0}}};
    context.SetTargets(targets, nullptr);
    context.SetState(PLAIN);
    DrawWhole(test, twoTargets, UNORM_COLOR);
    const std::array<std::uint8_t, 4> reversed = {UNORM_BYTES[3], UNORM_BYTES[2], UNORM_BYTES[1], UNORM_BYTES[0]};
    Assert::IsTrue(Read(test, first) == ExactTexels(TextureFormat::Rgba8, 6), L"the first target");
    Assert::IsTrue(Read(test, second) == Repeated(Bytes(reversed), 6), L"the second target");
    ExpectClean(test);
  }

  TEST_METHOD(ForgetsWhatGoes)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    Neuron::DrawContext& context = test.device.Context();
    context.SetState(PLAIN);
    {
      Texture gone = MakeTexture(test, TextureFormat::R8, 2, 2);
      SetTarget(test, gone);
    }
    DrawWhole(test, solid, WHITE);
    Assert::AreEqual(std::size_t{1}, test.failures.size(), L"a draw into a target that went was not refused");

    Texture target = MakeTexture(test, TextureFormat::R8, 2, 2);
    SetTarget(test, target);
    {
      Program brief = MakeProgram(test, Bytecode(SOLID_PS), "Brief");
      DrawWhole(test, brief, WHITE);
      Assert::AreEqual(std::size_t{1}, test.device.PipelineStates());
    }
    // Its pipeline state went with it, and it is no longer set.
    Assert::AreEqual(std::size_t{0}, test.device.PipelineStates(), L"the program's pipeline state stayed");
    context.DrawTransient(Bytes(WHOLE), POSITIONS, Bytes(FRONT_FACING), IndexFormat::UInt16);
    Assert::AreEqual(std::size_t{2}, test.failures.size(), L"a draw with a program that went was not refused");
    test.failures.clear();
    // The draw recorded before the program went still runs, with the state it held.
    test.device.WaitIdle();
    ExpectClean(test);
  }

  TEST_METHOD(RefusesWhatItCannotDraw)
  {
    TestDevice test;
    Open(test);
    Program solid = MakeProgram(test, Bytecode(SOLID_PS), "Solid");
    Program twoTargets = MakeProgram(test, Bytecode(TWO_TARGETS_PS), "TwoTargets");
    Program compute =
      test.device.CreateProgram({.vertexShader = {}, .pixelShader = {}, .computeShader = Bytecode(NAMES_CS), .name = "Compute"});
    Texture large = MakeTexture(test, TextureFormat::R8, 4, 4);
    Texture small = MakeTexture(test, TextureFormat::R8, 2, 2);
    Texture depth = MakeTexture(test, TextureFormat::Depth32F, 2, 2);
    Neuron::Buffer vertices = test.device.CreateBuffer({.sizeBytes = sizeof(WHOLE), .strideBytes = 12, .name = "DrawCalls vertices"});
    Neuron::Buffer indices = test.device.CreateBuffer({.sizeBytes = 6, .strideBytes = 2, .name = "DrawCalls indices"});
    Neuron::DrawContext& context = test.device.Context();
    context.SetState(PLAIN);

    const std::array<ColorTarget, 2> mixedSizes = {{{&large, 0, 0}, {&small, 0, 0}}};
    context.SetTargets(mixedSizes, nullptr); // targets of two sizes
    SetTarget(test, depth);                  // depth as colour
    context.SetProgram(compute);             // a program that draws nothing

    SetTarget(test, large);
    DrawWhole(test, twoTargets, WHITE); // two targets written, one set
    const std::array<VertexAttribute, 1> noPosition = {{{"TEXCOORD", 0, VertexFormat::Float3, 0}}};
    context.SetProgram(solid);
    context.DrawTransient(Bytes(WHOLE), {noPosition, 12}, Bytes(FRONT_FACING), IndexFormat::UInt16); // an input not in the layout
    context.DrawIndexed(vertices, POSITIONS, indices, IndexFormat::UInt16, 1, 3);                    // indices past the buffer

    SetTarget(test, large, &depth);
    context.SetState({.blend = BlendMode::Opaque, .cull = CullMode::None, .depthTest = true, .depthWrite = true, .wireframe = false});
    DrawWhole(test, solid, WHITE); // a depth target of another size

    Assert::AreEqual(std::size_t{7}, test.failures.size(), L"not every refusal was reported");
    test.failures.clear();
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
