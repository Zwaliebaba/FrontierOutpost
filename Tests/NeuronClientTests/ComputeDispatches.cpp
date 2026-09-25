// Tests/NeuronClientTests/ComputeDispatches.cpp
//
// A compute program writes a 3D field through a UAV from a texture it reads and its constants, as
// the SDF interpreter will; dispatches that write the same texture one after the other each see the
// writes before; and the context refuses what it cannot dispatch (Design/ADR/ADR-007, ADR-009; plan
// Phase 3).
#include "pch.h"

#include "Check.h"
#include "CompiledShaders/AccumulateCS.h"
#include "CompiledShaders/FieldCS.h"
#include "CompiledShaders/SolidPS.h"
#include "CompiledShaders/SolidVS.h"
#include "DrawContext.h"
#include "GraphicsDevice.h"
#include "Program.h"
#include "TestDevice.h"
#include "Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Neuron::Program;
using Neuron::Texture;
using Neuron::TextureDimension;
using Neuron::TextureFormat;

/// FXC's header holds the bytecode as an array of bytes.
template <typename T, std::size_t Count> std::span<const std::byte> Bytecode(const T (&_bytecode)[Count])
{
  return std::as_bytes(std::span(_bytecode));
}

template <typename T> std::span<const std::byte> Bytes(const T& _value)
{
  return std::as_bytes(std::span(&_value, 1));
}

Program MakeCompute(TestDevice& _test, std::span<const std::byte> _computeShader, std::string_view _name)
{
  Program program = _test.device.CreateProgram({.vertexShader = {}, .pixelShader = {}, .computeShader = _computeShader, .name = _name});
  Assert::IsTrue(static_cast<bool>(program), L"the program was not made");
  return program;
}

Texture MakeTexture(TestDevice& _test, TextureDimension _dimension, std::uint32_t _width, std::uint32_t _height, std::uint32_t _depth,
                    std::uint32_t _mips)
{
  Texture texture = _test.device.CreateTexture({.dimension = _dimension,
                                                .format = TextureFormat::R32F,
                                                .widthPixels = _width,
                                                .heightPixels = _height,
                                                .depthPixels = _depth,
                                                .mipLevels = _mips,
                                                .name = "ComputeDispatches"});
  Assert::IsTrue(static_cast<bool>(texture), L"the texture was not made");
  return texture;
}

std::vector<std::byte> Floats(const std::vector<float>& _values)
{
  std::vector<std::byte> bytes(_values.size() * sizeof(float));
  std::memcpy(bytes.data(), _values.data(), bytes.size());
  return bytes;
}

std::vector<std::byte> Read(TestDevice& _test, const Texture& _texture)
{
  std::vector<std::byte> texels;
  Assert::IsTrue(_test.device.Context().ReadTexture(_texture, 0, 0, texels), L"ReadTexture failed");
  return texels;
}

} // namespace

TEST_CLASS(ComputeDispatches)
{
public:
  TEST_METHOD(WritesA3DField)
  {
    TestDevice test;
    Open(test);
    Program fieldProgram = MakeCompute(test, Bytecode(FIELD_CS), "Field");
    Texture offsets = MakeTexture(test, TextureDimension::Texture2D, 4, 4, 1, 1);
    Texture field = MakeTexture(test, TextureDimension::Texture3D, 4, 4, 3, 1);
    Neuron::DrawContext& context = test.device.Context();
    // Offsets of x + 10 y, and slices 100 apart.
    std::vector<float> planeValues;
    for (int y = 0; y < 4; ++y)
    {
      for (int x = 0; x < 4; ++x)
      {
        planeValues.push_back(static_cast<float>(x + (10 * y)));
      }
    }
    context.UpdateTexture(offsets, 0, 0, Floats(planeValues));
    fieldProgram.SetConstant("size", Bytes(std::array<std::uint32_t, 3>{4, 4, 3}));
    fieldProgram.SetConstant("sliceStride", Bytes(100.0f));
    context.SetTexture(0, &offsets);
    context.SetUnorderedTexture(0, &field, 0);
    context.Dispatch(fieldProgram, 1, 1, 1);

    std::vector<float> fieldValues;
    for (int z = 0; z < 3; ++z)
    {
      for (const float value : planeValues)
      {
        fieldValues.push_back(value + (100.0f * static_cast<float>(z)));
      }
    }
    Assert::IsTrue(Read(test, field) == Floats(fieldValues), L"the field was not written as the offsets and slices give it");
    ExpectClean(test);
  }

  TEST_METHOD(OrdersDispatchesThatWriteTheSameTexture)
  {
    TestDevice test;
    Open(test);
    Program accumulate = MakeCompute(test, Bytecode(ACCUMULATE_CS), "Accumulate");
    // With mips, so that it can be written so.
    Texture total = MakeTexture(test, TextureDimension::Texture2D, 8, 8, 1, Texture::FullMipLevels(8, 8, 1));
    Neuron::DrawContext& context = test.device.Context();
    context.ClearColor(total, 0, 0, {0.0f, 0.0f, 0.0f, 0.0f});
    accumulate.SetConstant("size", Bytes(std::array<std::uint32_t, 2>{8, 8}));
    accumulate.SetConstant("amount", Bytes(1.0f));
    context.SetUnorderedTexture(0, &total, 0);
    for (int dispatch = 0; dispatch < 5; ++dispatch)
    {
      context.Dispatch(accumulate, 1, 1, 1);
    }
    Assert::IsTrue(Read(test, total) == Floats(std::vector<float>(64, 5.0f)), L"a dispatch did not see the one before");
    ExpectClean(test);
  }

  TEST_METHOD(RefusesWhatItCannotDispatch)
  {
    TestDevice test;
    Open(test);
    Program fieldProgram = MakeCompute(test, Bytecode(FIELD_CS), "Field");
    Program graphics = test.device.CreateProgram(
      {.vertexShader = Bytecode(SOLID_VS), .pixelShader = Bytecode(SOLID_PS), .computeShader = {}, .name = "Solid"});
    Texture flat = MakeTexture(test, TextureDimension::Texture2D, 4, 4, 1, 1);
    Texture field = MakeTexture(test, TextureDimension::Texture3D, 4, 4, 3, 1);
    Neuron::DrawContext& context = test.device.Context();

    context.Dispatch(graphics, 1, 1, 1);       // a program that draws
    context.Dispatch(fieldProgram, 1, 1, 1);   // u0 is not set
    context.SetUnorderedTexture(0, &flat, 0);  // a texture without mips
    context.SetUnorderedTexture(8, &field, 0); // past u7
    context.SetUnorderedTexture(0, &field, 0); // which is right, and
    context.Dispatch(fieldProgram, 0, 1, 1);   // no groups
    context.SetTexture(0, &field);             // what it writes, read as well
    context.Dispatch(fieldProgram, 1, 1, 1);
    Assert::AreEqual(std::size_t{6}, test.failures.size(), L"not every refusal was reported");
    test.failures.clear();
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
