// NeuronClient/Program.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

struct GraphicsCore;

enum class ShaderStage : std::uint8_t
{
  Vertex,
  Pixel,
  Compute
};

/// Where a constant a program reads sits in one stage's $Globals.
struct ProgramConstant
{
  ShaderStage stage;
  std::uint32_t offsetBytes;
  std::uint32_t sizeBytes;
};

/// One input the vertex shader reads, as an input layout matches it.
struct ProgramInput
{
  std::string semantic; // POSITION, TEXCOORD and so on
  std::uint32_t index;  // TEXCOORD3's 3
};

/// A vertex and pixel shader, or a compute shader, compiled by FXC at build time, and what
/// D3DReflect says of them at load (Design/ADR/ADR-008). A program reads its constants from each
/// stage's $Globals at b0, its textures and structured buffers from t0 to t15, its samplers from s0
/// to s15, and, for compute, what it writes from u0 to u7. Names are liblt's GLSL names: HLSL
/// reserves texture and sample and has saturate and noise as intrinsics, so the HLSL spells those
/// four with a trailing underscore, and the lookups here map them. It keeps its constants' values
/// on the CPU, as GL kept a program's uniforms, and each draw takes them as they are then.
class Program
{
public:
  static constexpr std::uint32_t MAX_SHADER_RESOURCES = 16;
  static constexpr std::uint32_t MAX_SAMPLERS = 16;
  static constexpr std::uint32_t MAX_UNORDERED_RESOURCES = 8;

  struct Desc
  {
    std::span<const std::byte> vertexShader;  // FXC's bytecode; empty for a compute program
    std::span<const std::byte> pixelShader;   // empty for a compute program
    std::span<const std::byte> computeShader; // empty for a graphics program
    std::string_view name;                    // for messages, and PIX
  };

  /// The name HLSL gives _glslName: texture, sample, saturate and noise gain a trailing
  /// underscore, and every other name is its own.
  [[nodiscard]] static std::string_view HlslName(std::string_view _glslName) noexcept;

  Program() noexcept;
  /// Hands the program's pipeline states to its device, which releases them once the GPU has
  /// finished with them, and unsets it where the context has it set.
  ~Program();
  Program(Program&& _other) noexcept;
  Program& operator=(Program&& _other) noexcept;
  Program(const Program&) = delete;
  Program& operator=(const Program&) = delete;

  /// False for a program that was never made, or that could not be.
  explicit operator bool() const noexcept;

  [[nodiscard]] bool IsCompute() const noexcept;

  /// The bytes of _stage's $Globals, rounded up to 16 as FXC lays it out; 0 when it has none.
  [[nodiscard]] std::uint32_t ConstantBytes(ShaderStage _stage) const noexcept;

  /// Where the constant _name sits in each stage that reads it: none, one or both of vertex and
  /// pixel.
  [[nodiscard]] std::vector<ProgramConstant> FindConstant(std::string_view _name) const;

  /// Sets the constant at each of _where, as FindConstant gave them, to _bytes. _bytes may be
  /// shorter than the constant, and its values stay until they are set again. One longer than the
  /// constant is reported, and nothing is set.
  void SetConstant(std::span<const ProgramConstant> _where, std::span<const std::byte> _bytes);

  /// Sets the constant _name, in each stage that reads it, as the other SetConstant does. A name no
  /// stage reads is not an error, as it was not in GL: nothing is set, and it returns false.
  bool SetConstant(std::string_view _name, std::span<const std::byte> _bytes);

  /// The t register of the texture or structured buffer _name, or -1 when no stage reads it.
  [[nodiscard]] int ShaderResourceSlot(std::string_view _name) const noexcept;

  /// The s register of the sampler _name, or -1.
  [[nodiscard]] int SamplerSlot(std::string_view _name) const noexcept;

  /// The u register of what the compute shader writes, _name, or -1.
  [[nodiscard]] int UnorderedSlot(std::string_view _name) const noexcept;

  /// How many render targets the pixel shader writes: SV_Target0 up to this.
  [[nodiscard]] std::uint32_t TargetCount() const noexcept;

  /// The vertex shader's inputs, in its signature's order.
  [[nodiscard]] std::span<const ProgramInput> Inputs() const noexcept;

private:
  friend class DrawContext;
  friend class GraphicsDevice;
  friend struct GraphicsCore;
  struct Native;

  /// Reflects the stages, or reports why the program cannot be used and returns an empty one.
  [[nodiscard]] static Program Make(const std::shared_ptr<GraphicsCore>& _core, const Desc& _desc);

  /// Hands the program to its device, as the destructor does.
  void Release() noexcept;

  std::shared_ptr<GraphicsCore> m_core; // the device's, which the program keeps alive
  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
