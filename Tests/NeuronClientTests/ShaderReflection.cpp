// Tests/NeuronClientTests/ShaderReflection.cpp
//
// d3dcompiler_47.dll loads, and D3DReflect reads a blob FXC compiled at build time: its constants,
// texture and sampler by name, and its input signature (Design/ADR/ADR-008, decision 9). The owner
// runs this on the ARM64 device as well (ADR-006). The project delay-loads the DLL, so that a target
// without it fails this test, naming the file, rather than failing to load the whole library.
#include "pch.h"

#include <d3d12shader.h>
#include <d3dcompiler.h>

#include "Check.h"
#include "CompiledShaders/ProbePS.h"

#include <memory>
#include <type_traits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Microsoft::WRL::ComPtr;

using Library = std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>;

void ExpectVariable(ID3D12ShaderReflectionConstantBuffer* _buffer, const char* _name, UINT _offsetBytes, UINT _sizeBytes)
{
  D3D12_SHADER_VARIABLE_DESC desc{};
  Check(_buffer->GetVariableByName(_name)->GetDesc(&desc), L"GetVariableByName");
  Assert::AreEqual(_name, desc.Name);
  Assert::AreEqual(_offsetBytes, desc.StartOffset);
  Assert::AreEqual(_sizeBytes, desc.Size);
}

void ExpectBinding(ID3D12ShaderReflection* _reflection, const char* _name, D3D_SHADER_INPUT_TYPE _type, UINT _bindPoint)
{
  // An out parameter only, read after Check: its return type has no zero value for {} to give.
  D3D12_SHADER_INPUT_BIND_DESC desc;
  Check(_reflection->GetResourceBindingDescByName(_name, &desc), L"GetResourceBindingDescByName");
  Assert::AreEqual(static_cast<int>(_type), static_cast<int>(desc.Type));
  Assert::AreEqual(_bindPoint, desc.BindPoint);
}

} // namespace

TEST_CLASS(ShaderReflection)
{
public:
  TEST_METHOD(ReflectsACompiledBlob)
  {
    // From System32, before the first call through the delay-loaded import.
    const Library compiler(LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), &FreeLibrary);
    Assert::IsNotNull(compiler.get(), L"d3dcompiler_47.dll did not load");

    ComPtr<ID3D12ShaderReflection> reflection;
    Check(D3DReflect(PROBE_PS, sizeof(PROBE_PS), IID_PPV_ARGS(&reflection)), L"D3DReflect");

    // Loose globals are gathered into $Globals.
    ID3D12ShaderReflectionConstantBuffer* const globals = reflection->GetConstantBufferByName("$Globals");
    D3D12_SHADER_BUFFER_DESC globalsDesc{};
    Check(globals->GetDesc(&globalsDesc), L"GetConstantBufferByName for $Globals");
    Assert::AreEqual(2u, globalsDesc.Variables);
    ExpectVariable(globals, "tint", 0, 16);
    ExpectVariable(globals, "gain", 16, 4);

    ExpectBinding(reflection.Get(), "image", D3D_SIT_TEXTURE, 0);
    ExpectBinding(reflection.Get(), "imageSampler", D3D_SIT_SAMPLER, 0);

    D3D12_SHADER_DESC shaderDesc{};
    Check(reflection->GetDesc(&shaderDesc), L"GetDesc");
    Assert::AreEqual(2u, shaderDesc.InputParameters);
    D3D12_SIGNATURE_PARAMETER_DESC uv{};
    Check(reflection->GetInputParameterDesc(1, &uv), L"GetInputParameterDesc");
    Assert::AreEqual("TEXCOORD", uv.SemanticName);
    Assert::AreEqual(0u, uv.SemanticIndex);
  }
};

} // namespace NeuronClientTests
