// Tests/NeuronClientTests/ProgramReflection.cpp
//
// A program made from FXC's bytecode finds its constants, textures and samplers by name, in each
// stage that reads them, and knows liblt's names for the four HLSL spells otherwise. It reads its
// input signature and the targets it writes, and refuses the stages the core cannot bind, saying
// why (Design/ADR/ADR-008; plan Phase 3).
#include "pch.h"

#include "Check.h"
#include "CompiledShaders/NamedBufferPS.h"
#include "CompiledShaders/NamesCS.h"
#include "CompiledShaders/NamesPS.h"
#include "CompiledShaders/NamesVS.h"
#include "GraphicsDevice.h"
#include "Program.h"
#include "TestDevice.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Neuron::Program;
using Neuron::ProgramConstant;
using Neuron::ProgramInput;
using Neuron::ShaderStage;

/// FXC's header holds the bytecode as an array of BYTE.
template <std::size_t Bytes> std::span<const std::byte> Bytecode(const BYTE (&_bytecode)[Bytes])
{
  return std::as_bytes(std::span(_bytecode));
}

Program MakeNames(TestDevice& _test)
{
  Program program = _test.device.CreateProgram(
    {.vertexShader = Bytecode(NAMES_VS), .pixelShader = Bytecode(NAMES_PS), .computeShader = {}, .name = "Names"});
  Assert::IsTrue(static_cast<bool>(program), L"the program was not made");
  return program;
}

void ExpectConstant(const ProgramConstant& _constant, ShaderStage _stage, std::uint32_t _offsetBytes, std::uint32_t _sizeBytes)
{
  Assert::AreEqual(static_cast<int>(_stage), static_cast<int>(_constant.stage));
  Assert::AreEqual(_offsetBytes, _constant.offsetBytes);
  Assert::AreEqual(_sizeBytes, _constant.sizeBytes);
}

void ExpectInput(const ProgramInput& _input, const char* _semantic, std::uint32_t _index)
{
  Assert::AreEqual(_semantic, _input.semantic.c_str());
  Assert::AreEqual(_index, _input.index);
}

} // namespace

TEST_CLASS(ProgramReflection)
{
public:
  TEST_METHOD(FindsEachStagesConstantsByName)
  {
    TestDevice test;
    Open(test);
    const Program program = MakeNames(test);
    Assert::IsFalse(program.IsCompute());
    // A $Globals is laid out in whole 16-byte registers.
    Assert::AreEqual(80u, program.ConstantBytes(ShaderStage::Vertex));
    Assert::AreEqual(32u, program.ConstantBytes(ShaderStage::Pixel));
    Assert::AreEqual(0u, program.ConstantBytes(ShaderStage::Compute));

    // tint follows the vertex shader's transform, and comes first in the pixel shader.
    const std::vector<ProgramConstant> tint = program.FindConstant("tint");
    Assert::AreEqual(std::size_t{2}, tint.size());
    ExpectConstant(tint[0], ShaderStage::Vertex, 64, 16);
    ExpectConstant(tint[1], ShaderStage::Pixel, 0, 16);
    const std::vector<ProgramConstant> transform = program.FindConstant("transform");
    Assert::AreEqual(std::size_t{1}, transform.size());
    ExpectConstant(transform[0], ShaderStage::Vertex, 0, 64);
    Assert::IsTrue(program.FindConstant("missing").empty());
    ExpectClean(test);
  }

  TEST_METHOD(MapsTheNamesHlslSpellsOtherwise)
  {
    Assert::AreEqual("texture_", std::string(Program::HlslName("texture")).c_str());
    Assert::AreEqual("sample_", std::string(Program::HlslName("sample")).c_str());
    Assert::AreEqual("saturate_", std::string(Program::HlslName("saturate")).c_str());
    Assert::AreEqual("noise_", std::string(Program::HlslName("noise")).c_str());
    Assert::AreEqual("tint", std::string(Program::HlslName("tint")).c_str());

    TestDevice test;
    Open(test);
    const Program program = MakeNames(test);
    const std::vector<ProgramConstant> saturate = program.FindConstant("saturate");
    Assert::AreEqual(std::size_t{1}, saturate.size());
    ExpectConstant(saturate[0], ShaderStage::Pixel, 16, 4);
    Assert::AreEqual(0, program.ShaderResourceSlot("texture"));
    Assert::AreEqual(3, program.ShaderResourceSlot("noise"));
    Assert::AreEqual(1, program.SamplerSlot("sample"));
    ExpectClean(test);
  }

  TEST_METHOD(FindsSamplersTargetsAndInputs)
  {
    TestDevice test;
    Open(test);
    const Program program = MakeNames(test);
    Assert::AreEqual(0, program.SamplerSlot("clamped"));
    // Each name is found only as what it is.
    Assert::AreEqual(-1, program.ShaderResourceSlot("clamped"));
    Assert::AreEqual(-1, program.SamplerSlot("texture"));
    Assert::AreEqual(-1, program.UnorderedSlot("texture"));
    Assert::AreEqual(2u, program.TargetCount());

    // SV_VertexID comes from no buffer, so it is not among them.
    const std::span<const ProgramInput> inputs = program.Inputs();
    Assert::AreEqual(std::size_t{4}, inputs.size());
    ExpectInput(inputs[0], "POSITION", 0);
    ExpectInput(inputs[1], "TEXCOORD", 0);
    ExpectInput(inputs[2], "COLOR", 0);
    ExpectInput(inputs[3], "TEXCOORD", 3);
    ExpectClean(test);
  }

  TEST_METHOD(ReflectsAComputeProgram)
  {
    TestDevice test;
    Open(test);
    const Program program =
      test.device.CreateProgram({.vertexShader = {}, .pixelShader = {}, .computeShader = Bytecode(NAMES_CS), .name = "NamesCompute"});
    Assert::IsTrue(static_cast<bool>(program), L"the program was not made");
    Assert::IsTrue(program.IsCompute());
    Assert::AreEqual(16u, program.ConstantBytes(ShaderStage::Compute));
    const std::vector<ProgramConstant> size = program.FindConstant("size");
    Assert::AreEqual(std::size_t{1}, size.size());
    ExpectConstant(size[0], ShaderStage::Compute, 0, 12);
    Assert::AreEqual(1, program.ShaderResourceSlot("source"));
    Assert::AreEqual(2, program.UnorderedSlot("field"));
    Assert::AreEqual(0u, program.TargetCount());
    Assert::IsTrue(program.Inputs().empty());
    ExpectClean(test);
  }

  TEST_METHOD(RefusesStagesThatMakeNoProgram)
  {
    TestDevice test;
    Open(test);
    const std::span<const std::byte> vertex = Bytecode(NAMES_VS);
    const std::span<const std::byte> pixel = Bytecode(NAMES_PS);
    const std::span<const std::byte> compute = Bytecode(NAMES_CS);
    const std::array<Program::Desc, 5> descs = {{
      {.vertexShader = vertex, .pixelShader = {}, .computeShader = {}, .name = "VertexAlone"},
      {.vertexShader = {}, .pixelShader = pixel, .computeShader = {}, .name = "PixelAlone"},
      {.vertexShader = {}, .pixelShader = {}, .computeShader = {}, .name = "Nothing"},
      {.vertexShader = vertex, .pixelShader = {}, .computeShader = compute, .name = "VertexAndCompute"},
      {.vertexShader = vertex, .pixelShader = pixel, .computeShader = compute, .name = "Everything"},
    }};
    for (const Program::Desc& desc : descs)
    {
      const Program program = test.device.CreateProgram(desc);
      Assert::IsFalse(static_cast<bool>(program), Widen(desc.name).c_str());
    }
    Assert::AreEqual(descs.size(), test.failures.size(), L"not every refusal was reported");
    test.failures.clear();
    ExpectClean(test);
  }

  TEST_METHOD(RefusesACbufferOtherThanGlobals)
  {
    TestDevice test;
    Open(test);
    const Program program = test.device.CreateProgram(
      {.vertexShader = Bytecode(NAMES_VS), .pixelShader = Bytecode(NAMED_BUFFER_PS), .computeShader = {}, .name = "NamedBuffer"});
    Assert::IsFalse(static_cast<bool>(program));
    Assert::AreEqual(std::size_t{1}, test.failures.size(), L"the refusal was not reported");
    Assert::IsTrue(test.failures.front().find("cbuffer Lighting") != std::string::npos, Widen(test.failures.front()).c_str());
    test.failures.clear();
    ExpectClean(test);
  }
};

} // namespace NeuronClientTests
