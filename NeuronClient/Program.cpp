// NeuronClient/Program.cpp
#include "pch.h"

#include <d3d12shader.h>
#include <d3dcompiler.h>

#include "GraphicsCore.h"
#include "Program.h"

#include <algorithm>
#include <array>
#include <exception>
#include <format>
#include <utility>

#pragma comment(lib, "d3dcompiler.lib")

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// liblt's GLSL names that HLSL will not take, and what the HLSL calls them instead.
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> RENAMED = {
  {{"texture", "texture_"}, {"sample", "sample_"}, {"saturate", "saturate_"}, {"noise", "noise_"}}};

std::string_view StageName(ShaderStage _stage) noexcept
{
  switch (_stage)
  {
  case ShaderStage::Vertex:
    return "vertex shader";
  case ShaderStage::Pixel:
    return "pixel shader";
  case ShaderStage::Compute:
    return "compute shader";
  }
  return "shader";
}

/// Records that _name is at register _slot, which must be below _limit. A name both stages read
/// must be at the same register in both.
bool Bind(std::map<std::string, int, std::less<>>& _registers, std::string_view _name, UINT _slot, std::uint32_t _limit, char _prefix,
          std::string& _error)
{
  if (_slot >= _limit)
  {
    _error = std::format("{} is at {}{}, past {}{}", _name, _prefix, _slot, _prefix, _limit - 1);
    return false;
  }
  const auto [found, added] = _registers.emplace(std::string(_name), static_cast<int>(_slot));
  if (!added && found->second != static_cast<int>(_slot))
  {
    _error = std::format("{} is at {}{} in one stage and {}{} in the other", _name, _prefix, found->second, _prefix, _slot);
    return false;
  }
  return true;
}

} // namespace

bool Program::Native::Reflect(std::span<const std::byte> _bytecode, ShaderStage _stage, std::string& _error)
{
  const std::string_view stageName = StageName(_stage);
  ComPtr<ID3D12ShaderReflection> reflection;
  HRESULT result = D3DReflect(_bytecode.data(), _bytecode.size(), IID_PPV_ARGS(&reflection));
  D3D12_SHADER_DESC shaderDesc{};
  if (SUCCEEDED(result))
  {
    result = reflection->GetDesc(&shaderDesc);
  }
  if (FAILED(result))
  {
    _error = std::format("the {} does not reflect (0x{:08x})", stageName, static_cast<unsigned long>(result));
    return false;
  }

  Constants& stageConstants = constants[static_cast<std::size_t>(_stage)];
  for (UINT index = 0; index < shaderDesc.ConstantBuffers; ++index)
  {
    ID3D12ShaderReflectionConstantBuffer* const buffer = reflection->GetConstantBufferByIndex(index);
    D3D12_SHADER_BUFFER_DESC bufferDesc{};
    if (FAILED(buffer->GetDesc(&bufferDesc)))
    {
      _error = std::format("a constant buffer of the {} does not reflect", stageName);
      return false;
    }
    if (bufferDesc.Type != D3D_CT_CBUFFER)
    {
      continue; // a structured buffer's element, which is no constant
    }
    if (std::string_view(bufferDesc.Name) != "$Globals")
    {
      _error = std::format("the {} declares cbuffer {}; only $Globals, at b0, is bound", stageName, bufferDesc.Name);
      return false;
    }
    stageConstants.sizeBytes = bufferDesc.Size;
    for (UINT variable = 0; variable < bufferDesc.Variables; ++variable)
    {
      D3D12_SHADER_VARIABLE_DESC variableDesc{};
      if (FAILED(buffer->GetVariableByIndex(variable)->GetDesc(&variableDesc)))
      {
        _error = std::format("a constant of the {} does not reflect", stageName);
        return false;
      }
      stageConstants.variables.emplace(variableDesc.Name, ProgramConstant{_stage, variableDesc.StartOffset, variableDesc.Size});
    }
  }

  for (UINT index = 0; index < shaderDesc.BoundResources; ++index)
  {
    // An out parameter only: its return type has no zero value for {} to give.
    D3D12_SHADER_INPUT_BIND_DESC bind;
    if (FAILED(reflection->GetResourceBindingDesc(index, &bind)))
    {
      _error = std::format("a binding of the {} does not reflect", stageName);
      return false;
    }
    const std::string_view boundName(bind.Name);
    if (bind.Space != 0 || bind.BindCount != 1)
    {
      _error = std::format("the {} binds {} in space {} as {} registers; the core binds single registers in space 0", stageName, boundName,
                           bind.Space, bind.BindCount);
      return false;
    }
    bool bound = true;
    switch (bind.Type)
    {
    case D3D_SIT_CBUFFER:
      if (bind.BindPoint != 0)
      {
        _error = std::format("the {} binds {} at b{}; only b0 is bound", stageName, boundName, bind.BindPoint);
        bound = false;
      }
      break;
    case D3D_SIT_TEXTURE:
    case D3D_SIT_STRUCTURED:
    case D3D_SIT_BYTEADDRESS:
      bound = Bind(shaderResources, boundName, bind.BindPoint, MAX_SHADER_RESOURCES, 't', _error);
      break;
    case D3D_SIT_SAMPLER:
      bound = Bind(samplers, boundName, bind.BindPoint, MAX_SAMPLERS, 's', _error);
      break;
    case D3D_SIT_UAV_RWTYPED:
    case D3D_SIT_UAV_RWSTRUCTURED:
    case D3D_SIT_UAV_RWBYTEADDRESS:
      bound = Bind(unorderedResources, boundName, bind.BindPoint, MAX_UNORDERED_RESOURCES, 'u', _error);
      break;
    default:
      _error = std::format("the {} binds {}, a kind of resource the core does not bind", stageName, boundName);
      bound = false;
      break;
    }
    if (!bound)
    {
      return false;
    }
  }

  if (_stage == ShaderStage::Vertex)
  {
    for (UINT index = 0; index < shaderDesc.InputParameters; ++index)
    {
      D3D12_SIGNATURE_PARAMETER_DESC parameter{};
      if (FAILED(reflection->GetInputParameterDesc(index, &parameter)))
      {
        _error = "an input of the vertex shader does not reflect";
        return false;
      }
      // SV_VertexID and its kind come from no buffer.
      if (parameter.SystemValueType == D3D_NAME_UNDEFINED)
      {
        inputs.push_back({parameter.SemanticName, parameter.SemanticIndex});
      }
    }
  }
  if (_stage == ShaderStage::Pixel)
  {
    for (UINT index = 0; index < shaderDesc.OutputParameters; ++index)
    {
      D3D12_SIGNATURE_PARAMETER_DESC parameter{};
      if (FAILED(reflection->GetOutputParameterDesc(index, &parameter)))
      {
        _error = "an output of the pixel shader does not reflect";
        return false;
      }
      if (parameter.SystemValueType == D3D_NAME_TARGET)
      {
        targetCount = std::max(targetCount, parameter.SemanticIndex + 1);
      }
    }
  }
  return true;
}

std::string_view Program::HlslName(std::string_view _glslName) noexcept
{
  for (const auto& [glsl, hlsl] : RENAMED)
  {
    if (glsl == _glslName)
    {
      return hlsl;
    }
  }
  return _glslName;
}

Program Program::Make(const std::shared_ptr<GraphicsCore>& _core, const Desc& _desc)
{
  Program program;
  GraphicsCore& core = *_core;
  const bool compute = !_desc.computeShader.empty();
  const bool graphics = !_desc.vertexShader.empty() && !_desc.pixelShader.empty();
  if (compute == graphics || (compute && (!_desc.vertexShader.empty() || !_desc.pixelShader.empty())))
  {
    core.Fail(std::format("Direct3D 12: program {} needs a vertex and a pixel shader, or a compute shader alone", _desc.name));
    return program;
  }
  auto native = std::make_unique<Native>();
  native->name = std::string(_desc.name);
  native->id = core.nextProgramId++;
  native->compute = compute;
  native->vertexShader.assign(_desc.vertexShader.begin(), _desc.vertexShader.end());
  native->pixelShader.assign(_desc.pixelShader.begin(), _desc.pixelShader.end());
  native->computeShader.assign(_desc.computeShader.begin(), _desc.computeShader.end());
  std::string error;
  const bool reflected = compute ? native->Reflect(native->computeShader, ShaderStage::Compute, error)
                                 : native->Reflect(native->vertexShader, ShaderStage::Vertex, error) &&
                                     native->Reflect(native->pixelShader, ShaderStage::Pixel, error);
  if (!reflected)
  {
    core.Fail(std::format("Direct3D 12: program {}: {}", native->name, error));
    return program;
  }
  // The values start at zero, as GL's uniforms did.
  for (std::size_t stage = 0; stage < native->constants.size(); ++stage)
  {
    native->constantValues[stage].assign(native->constants[stage].sizeBytes, std::byte{0});
  }
  program.m_core = _core;
  program.m_native = std::move(native);
  return program;
}

Program::Program() noexcept = default;

Program::~Program()
{
  Release();
}

Program::Program(Program&& _other) noexcept = default;

Program& Program::operator=(Program&& _other) noexcept
{
  if (this != &_other)
  {
    Release();
    m_core = std::move(_other.m_core);
    m_native = std::move(_other.m_native);
  }
  return *this;
}

/// An exception cannot be reported from here, so one, which can only be memory running out, ends
/// the program.
void Program::Release() noexcept
{
  if (!m_native)
  {
    return;
  }
  try
  {
    m_core->ForgetProgram(*m_native);
  }
  catch (...)
  {
    std::terminate();
  }
  m_native.reset();
  m_core.reset();
}

Program::operator bool() const noexcept
{
  return m_native != nullptr;
}

bool Program::IsCompute() const noexcept
{
  return m_native && m_native->compute;
}

std::uint32_t Program::ConstantBytes(ShaderStage _stage) const noexcept
{
  return m_native ? m_native->constants[static_cast<std::size_t>(_stage)].sizeBytes : 0;
}

std::vector<ProgramConstant> Program::FindConstant(std::string_view _name) const
{
  std::vector<ProgramConstant> found;
  if (!m_native)
  {
    return found;
  }
  const std::string_view name = HlslName(_name);
  for (const Native::Constants& stage : m_native->constants)
  {
    if (const auto variable = stage.variables.find(name); variable != stage.variables.end())
    {
      found.push_back(variable->second);
    }
  }
  return found;
}

void Program::SetConstant(std::span<const ProgramConstant> _where, std::span<const std::byte> _bytes)
{
  if (!m_native)
  {
    return;
  }
  for (const ProgramConstant& constant : _where)
  {
    // One from another program's FindConstant could lie outside this one's $Globals.
    const std::size_t stageBytes = m_native->constantValues[static_cast<std::size_t>(constant.stage)].size();
    if (_bytes.size() > constant.sizeBytes || std::size_t{constant.offsetBytes} + constant.sizeBytes > stageBytes)
    {
      m_core->Fail(std::format("Direct3D 12: program {} was given {} bytes for a constant of {} at {}", m_native->name, _bytes.size(),
                               constant.sizeBytes, constant.offsetBytes));
      return;
    }
  }
  for (const ProgramConstant& constant : _where)
  {
    std::vector<std::byte>& values = m_native->constantValues[static_cast<std::size_t>(constant.stage)];
    std::ranges::copy(_bytes, values.begin() + constant.offsetBytes);
  }
}

bool Program::SetConstant(std::string_view _name, std::span<const std::byte> _bytes)
{
  const std::vector<ProgramConstant> where = FindConstant(_name);
  SetConstant(where, _bytes);
  return !where.empty();
}

int Program::ShaderResourceSlot(std::string_view _name) const noexcept
{
  if (!m_native)
  {
    return -1;
  }
  const auto found = m_native->shaderResources.find(HlslName(_name));
  return found != m_native->shaderResources.end() ? found->second : -1;
}

int Program::SamplerSlot(std::string_view _name) const noexcept
{
  if (!m_native)
  {
    return -1;
  }
  const auto found = m_native->samplers.find(HlslName(_name));
  return found != m_native->samplers.end() ? found->second : -1;
}

int Program::UnorderedSlot(std::string_view _name) const noexcept
{
  if (!m_native)
  {
    return -1;
  }
  const auto found = m_native->unorderedResources.find(HlslName(_name));
  return found != m_native->unorderedResources.end() ? found->second : -1;
}

std::uint32_t Program::TargetCount() const noexcept
{
  return m_native ? m_native->targetCount : 0;
}

std::span<const ProgramInput> Program::Inputs() const noexcept
{
  return m_native ? std::span<const ProgramInput>(m_native->inputs) : std::span<const ProgramInput>();
}

} // namespace Neuron
