// Tests/NeuronClientTests/Presentation.cpp
//
// One asymmetric image, taken through a render target, sampling and the present pass, comes out the
// right way up: what is drawn at the top left of GL's clip space is at the top left of the window.
// That pins down the flips of plan §5.5, the clip-space macro's and the present pass's. The swap
// chain also shows frame after frame, resizes, and refuses what it cannot show (plan Phase 3).
#include "pch.h"

#include "Check.h"
#include "CompiledShaders/SolidPS.h"
#include "CompiledShaders/SolidVS.h"
#include "CompiledShaders/TexturedPS.h"
#include "CompiledShaders/TexturedVS.h"
#include "DrawContext.h"
#include "ExactColors.h"
#include "GraphicsDevice.h"
#include "Program.h"
#include "SwapChain.h"
#include "TestDevice.h"
#include "Texture.h"
#include "Window.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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
using Neuron::SwapChain;
using Neuron::Texture;
using Neuron::TextureDimension;
using Neuron::TextureFilter;
using Neuron::TextureFormat;
using Neuron::TextureWrap;
using Neuron::VertexAttribute;
using Neuron::VertexFormat;
using Neuron::VertexLayout;

constexpr std::uint32_t WIDTH_PIXELS = 64;
constexpr std::uint32_t HEIGHT_PIXELS = 32;

constexpr std::array<VertexAttribute, 1> POSITION_ATTRIBUTES = {{{"POSITION", 0, VertexFormat::Float3, 0}}};
constexpr VertexLayout POSITIONS{POSITION_ATTRIBUTES, 3 * sizeof(float)};
constexpr std::array<VertexAttribute, 2> TEXTURED_ATTRIBUTES = {
  {{"POSITION", 0, VertexFormat::Float3, 0}, {"TEXCOORD", 0, VertexFormat::Float2, 3 * sizeof(float)}}};
constexpr VertexLayout TEXTURED{TEXTURED_ATTRIBUTES, 5 * sizeof(float)};
constexpr std::array<std::uint16_t, 6> QUAD_INDICES = {0, 1, 2, 0, 2, 3};

constexpr RenderState PLAIN{
  .blend = BlendMode::Opaque, .cull = CullMode::None, .depthTest = false, .depthWrite = false, .wireframe = false};

constexpr SamplerDesc NEAREST{.magFilter = TextureFilter::Nearest,
                              .minFilter = TextureFilter::Nearest,
                              .mipFilter = MipFilter::None,
                              .wrapU = TextureWrap::ClampToEdge,
                              .wrapV = TextureWrap::ClampToEdge,
                              .wrapW = TextureWrap::ClampToEdge,
                              .lodBias = 0.0f,
                              .minLod = 0.0f,
                              .maxLod = 0.0f,
                              .maxAnisotropy = 1,
                              .borderColor = {}};

constexpr std::array<std::uint8_t, 4> BLACK = {0, 0, 0, 255};
constexpr std::array<std::uint8_t, 4> WHITE = {255, 255, 255, 255};

/// FXC's header holds the bytecode as an array of bytes.
template <typename T, std::size_t Count> std::span<const std::byte> Bytecode(const T (&_bytecode)[Count])
{
  return std::as_bytes(std::span(_bytecode));
}

template <typename T> std::span<const std::byte> Bytes(const T& _value)
{
  return std::as_bytes(std::span(&_value, 1));
}

Neuron::Window OpenWindow()
{
  Neuron::Window window;
  std::string error;
  Assert::IsTrue(Neuron::Window::Open({.titleUtf8 = "NeuronClientTests",
                                       .widthPixels = WIDTH_PIXELS,
                                       .heightPixels = HEIGHT_PIXELS,
                                       .border = false,
                                       .cursorVisible = true},
                                      window, error),
                 Widen(error).c_str());
  return window;
}

SwapChain MakeSwapChain(TestDevice& _test, const Neuron::Window& _window)
{
  SwapChain swapChain =
    _test.device.CreateSwapChain({.window = &_window, .widthPixels = WIDTH_PIXELS, .heightPixels = HEIGHT_PIXELS, .vsync = false});
  Assert::IsTrue(static_cast<bool>(swapChain), L"the swap chain was not made");
  return swapChain;
}

Texture MakeTexture(TestDevice& _test, TextureDimension _dimension, TextureFormat _format, std::uint32_t _width, std::uint32_t _height)
{
  Texture texture = _test.device.CreateTexture({.dimension = _dimension,
                                                .format = _format,
                                                .widthPixels = _width,
                                                .heightPixels = _height,
                                                .depthPixels = 1,
                                                .mipLevels = 1,
                                                .name = "Presentation"});
  Assert::IsTrue(static_cast<bool>(texture), L"the texture was not made");
  return texture;
}

Program MakeProgram(TestDevice& _test, std::span<const std::byte> _vertexShader, std::span<const std::byte> _pixelShader,
                    std::string_view _name)
{
  Program program =
    _test.device.CreateProgram({.vertexShader = _vertexShader, .pixelShader = _pixelShader, .computeShader = {}, .name = _name});
  Assert::IsTrue(static_cast<bool>(program), L"the program was not made");
  return program;
}

/// What the window shows, top row first: _corner in the top left quarter, and _rest elsewhere.
std::vector<std::byte> WindowTexels(std::array<std::uint8_t, 4> _corner, std::array<std::uint8_t, 4> _rest)
{
  std::vector<std::byte> texels;
  for (std::uint32_t row = 0; row < HEIGHT_PIXELS; ++row)
  {
    for (std::uint32_t column = 0; column < WIDTH_PIXELS; ++column)
    {
      const bool corner = row < HEIGHT_PIXELS / 2 && column < WIDTH_PIXELS / 2;
      for (const std::uint8_t channel : corner ? _corner : _rest)
      {
        texels.push_back(static_cast<std::byte>(channel));
      }
    }
  }
  return texels;
}

} // namespace

TEST_CLASS(Presentation)
{
public:
  TEST_METHOD(ShowsTheTopLeftOfGlsClipSpaceAtTheTopLeftOfTheWindow)
  {
    TestDevice test;
    Open(test);
    const Neuron::Window window = OpenWindow();
    SwapChain swapChain = MakeSwapChain(test, window);
    Program solid = MakeProgram(test, Bytecode(SOLID_VS), Bytecode(SOLID_PS), "Solid");
    Program textured = MakeProgram(test, Bytecode(TEXTURED_VS), Bytecode(TEXTURED_PS), "Textured");
    Texture drawn = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::Rgba8, WIDTH_PIXELS, HEIGHT_PIXELS);
    Texture image = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::Rgba8, WIDTH_PIXELS, HEIGHT_PIXELS);
    Neuron::DrawContext& context = test.device.Context();
    test.device.BeginFrame();
    context.ClearColor(drawn, 0, 0, {0.0f, 0.0f, 0.0f, 1.0f});
    context.SetState(PLAIN);

    // White over the top left quarter of GL's clip space: x from -1 to 0, and y from 0 to 1.
    const std::array<ColorTarget, 1> toDrawn = {{{&drawn, 0, 0}}};
    context.SetTargets(toDrawn, nullptr);
    solid.SetConstant("color", Bytes(std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}));
    context.SetProgram(solid);
    const std::array<float, 12> corner = {-1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f};
    context.DrawTransient(Bytes(corner), POSITIONS, Bytes(QUAD_INDICES), IndexFormat::UInt16);

    // Sampled into the image texel for texel, with GL's texture coordinates: (0, 0) is row 0.
    const std::array<ColorTarget, 1> toImage = {{{&image, 0, 0}}};
    context.SetTargets(toImage, nullptr);
    context.SetProgram(textured);
    context.SetTexture(0, &drawn);
    context.SetSampler(0, NEAREST);
    const std::array<float, 20> whole = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
                                         1.0f,  1.0f,  0.0f, 1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 0.0f, 1.0f};
    context.DrawTransient(Bytes(whole), TEXTURED, Bytes(QUAD_INDICES), IndexFormat::UInt16);

    std::vector<std::byte> shown;
    Assert::IsTrue(swapChain.PresentAndCapture(image, shown), L"nothing was shown");
    test.device.EndFrame();
    Assert::IsTrue(shown == WindowTexels(WHITE, BLACK), L"the window does not show the image the right way up");
    ExpectClean(test);
  }

  TEST_METHOD(ShowsFrameAfterFrameAndResizes)
  {
    TestDevice test;
    Open(test);
    const Neuron::Window window = OpenWindow();
    SwapChain swapChain = MakeSwapChain(test, window);
    Texture image = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::Rgba8, WIDTH_PIXELS, HEIGHT_PIXELS);
    Neuron::DrawContext& context = test.device.Context();
    // More frames than buffers, so that each buffer is drawn into again.
    for (int frame = 0; frame < 5; ++frame)
    {
      test.device.BeginFrame();
      context.ClearColor(image, 0, 0, {0.0f, 0.0f, 0.0f, 1.0f});
      swapChain.Present(image);
      test.device.EndFrame();
    }

    // Half the size, as when the window is dragged smaller.
    constexpr std::uint32_t HALF_WIDTH = WIDTH_PIXELS / 2;
    constexpr std::uint32_t HALF_HEIGHT = HEIGHT_PIXELS / 2;
    swapChain.Resize(HALF_WIDTH, HALF_HEIGHT);
    Assert::AreEqual(HALF_WIDTH, swapChain.WidthPixels());
    Assert::AreEqual(HALF_HEIGHT, swapChain.HeightPixels());
    Texture smaller = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::Rgba8, HALF_WIDTH, HALF_HEIGHT);
    test.device.BeginFrame();
    context.ClearColor(smaller, 0, 0, UNORM_COLOR);
    std::vector<std::byte> shown;
    Assert::IsTrue(swapChain.PresentAndCapture(smaller, shown), L"nothing was shown after the resize");
    test.device.EndFrame();
    Assert::IsTrue(shown == ExactTexels(TextureFormat::Rgba8, std::size_t{HALF_WIDTH} * HALF_HEIGHT), L"the resized frame was not shown");

    // A minimized window's size is not taken.
    swapChain.Resize(0, 0);
    Assert::AreEqual(HALF_WIDTH, swapChain.WidthPixels());
    ExpectClean(test);
  }

  TEST_METHOD(RefusesWhatItCannotShow)
  {
    TestDevice test;
    Open(test);
    const SwapChain nowhere =
      test.device.CreateSwapChain({.window = nullptr, .widthPixels = WIDTH_PIXELS, .heightPixels = HEIGHT_PIXELS, .vsync = false});
    Assert::IsFalse(static_cast<bool>(nowhere), L"a swap chain was made without a window");
    const Neuron::Window window = OpenWindow();
    SwapChain swapChain = MakeSwapChain(test, window);
    Texture cube = MakeTexture(test, TextureDimension::TextureCube, TextureFormat::Rgba8, 4, 4);
    Texture depth = MakeTexture(test, TextureDimension::Texture2D, TextureFormat::Depth32F, 4, 4);
    std::vector<std::byte> shown;
    Assert::IsFalse(swapChain.PresentAndCapture(cube, shown), L"a cube was shown");
    Assert::IsFalse(swapChain.PresentAndCapture(depth, shown), L"a depth texture was shown");
    Assert::AreEqual(std::size_t{3}, test.failures.size(), L"not every refusal was reported");
    test.failures.clear();
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
