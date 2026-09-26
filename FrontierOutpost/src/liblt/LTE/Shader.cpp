#include "Shader.h"

#include "CubeMap.h"
#include "Map.h"
#include "Matrix.h"
#include "Pointer.h"
#include "Program.h"
#include "ProgramLog.h"
#include "Renderer.h"
#include "RendererCore.h"
#include "ShaderRegistry.h"
#include "Texture2D.h"
#include "Texture3D.h"
#include "V4.h"

#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

/* Programs from the shaders FXC compiled into lt.dll, found by the legacy names
 * Shader_Create is given (Design/ADR/ADR-008). What GL kept in a program, a
 * program here keeps: its constants, which NeuronClient's Program holds on the
 * CPU, and the texture each of its samplers was last given. Setting either makes
 * the program current, as GL's Use() did, and a draw takes the current program
 * as it is then (Design/Archive/NeuronClient-migration.md, section 5.4). */

const uint kTextureUnits = Neuron::Program::MAX_SHADER_RESOURCES;

/* HLSL lays each element of an array in $Globals out on its own 16 bytes. */
const size_t kArrayStrideBytes = 16;

namespace {
  typedef Reference<struct ProgramObjectT> ProgramObject;
  typedef Map<String, ProgramObject> ProgramMap;

  struct ProgramObjectT* gActiveProgram = nullptr;
  Pointer<ShaderT> gActiveShader;

  ProgramMap& GetProgramCache() {
    static ProgramMap map;
    return map;
  }

  /* A name a program was asked for: a constant, in one stage or both, or a
     texture and the sampler of its own that the shaders declare beside it. */
  struct Uniform {
    String name;
    std::vector<Neuron::ProgramConstant> constant;
    int textureSlot;
    int samplerSlot;
  };

  /* The texture a sampler holds, by its id (LTE/RendererCore.h), which does
     not keep it alive, as a GL texture unit did not. */
  struct TextureBinding {
    uint64 id;
    int samplerSlot;
  };

  struct ProgramObjectT : public RefCounted {
    Neuron::Program program;
    String vertPath;
    String fragPath;
    String path;
    Vector<Uniform> uniforms;
    Map<String, int> uniformIndex;
    std::array<TextureBinding, kTextureUnits> textures;
    std::vector<String> warned;
    int mWorld;
    int mView;
    int mProj;
    int mWorldIT;
    int mWVP;

    ProgramObjectT() :
      mWorld(-1),
      mView(-1),
      mProj(-1),
      mWorldIT(-1),
      mWVP(-1)
    {
      for (size_t i = 0; i < textures.size(); ++i) {
        textures[i].id = 0;
        textures[i].samplerSlot = -1;
      }
    }

    ~ProgramObjectT() {
      if (!Program_InStaticSection()) {
        if (gActiveProgram == this)
          Shader_UseFixedFunction();
        if (path.size() && GetProgramCache().get(path))
          GetProgramCache().erase(path);
      }
    }

    /* How a warning names the pair: the file each stage was compiled from, and
       the legacy name the game asked for it by (Design/ADR/ADR-008). */
    String Label() const {
      return Stringize() | "Shader(" | ShaderRegistry_SourceName(vertPath, "VS") |
        " as " | vertPath | ", " | ShaderRegistry_SourceName(fragPath, "PS") |
        " as " | fragPath | ")";
    }

    void CacheWVP() {
      mWorld = GetUniformLocation("WORLD", false);
      mView = GetUniformLocation("VIEW", false);
      mProj = GetUniformLocation("PROJ", false);
      mWorldIT = GetUniformLocation("WORLDIT", false);
      mWVP = GetUniformLocation("WVP", false);
    }

    /* Names are found once, by their text, and a name no stage reads is -1, as
       GL's location was. */
    int GetUniformLocation(char const* name, bool warn) {
      String const key(name);
      int* found = uniformIndex.get(key);
      if (found)
        return *found;

      Uniform uniform;
      uniform.name = key;
      uniform.constant = program.FindConstant(name);
      uniform.textureSlot = program.ShaderResourceSlot(name);
      uniform.samplerSlot = uniform.textureSlot >= 0
        ? program.SamplerSlot(std::string(Neuron::Program::HlslName(name)) + "Sampler")
        : -1;

      int index = -1;
      if (!uniform.constant.empty() || uniform.textureSlot >= 0) {
        index = (int)uniforms.size();
        uniforms.push(uniform);
      }
      uniformIndex[key] = index;

      if (warn && index < 0) {
        String warning = Stringize() | "Unused variable " | name | " in " | Label();
        Log_Warning(warning);
      }
      return index;
    }

    /* Once for each name, what GL refused with an error and nothing set. */
    void WarnOnce(Uniform const& uniform, char const* what) {
      for (size_t i = 0; i < warned.size(); ++i)
        if (warned[i] == uniform.name)
          return;
      warned.push_back(uniform.name);
      Log_Warning(Stringize() | Label() | ": " | uniform.name | " " | what | "; it was not set");
    }

    /* _size bytes to the constant at _varIndex, which GL refused when they did
       not fit it. */
    void SetConstant(int varIndex, void const* data, size_t size) {
      if (varIndex < 0 || varIndex >= (int)uniforms.size())
        return;
      Uniform const& uniform = uniforms[varIndex];
      if (uniform.constant.empty()) {
        WarnOnce(uniform, "is a texture, not a value");
        return;
      }
      for (size_t i = 0; i < uniform.constant.size(); ++i) {
        if (size > uniform.constant[i].sizeBytes) {
          WarnOnce(uniform, "is smaller than the value given");
          return;
        }
      }
      program.SetConstant(uniform.constant,
        std::span<std::byte const>((std::byte const*)data, size));
    }

    /* An array of _count elements of _elementBytes each, laid out as HLSL lays
       out an array: each element on 16 bytes of its own. Elements past the
       array's end are left out, as GL left them. */
    void SetArray(int varIndex, void const* data, size_t count, size_t elementBytes) {
      if (varIndex < 0 || varIndex >= (int)uniforms.size() || !count)
        return;
      Uniform const& uniform = uniforms[varIndex];
      if (uniform.constant.empty()) {
        WarnOnce(uniform, "is a texture, not a value");
        return;
      }
      size_t capacity = count;
      for (size_t i = 0; i < uniform.constant.size(); ++i) {
        size_t const bytes = uniform.constant[i].sizeBytes;
        size_t const fits = bytes < elementBytes ? 0 : 1 + (bytes - elementBytes) / kArrayStrideBytes;
        capacity = Min(capacity, fits);
      }
      if (!capacity) {
        WarnOnce(uniform, "is smaller than the value given");
        return;
      }
      std::vector<std::byte> packed((capacity - 1) * kArrayStrideBytes + elementBytes);
      for (size_t i = 0; i < capacity; ++i)
        std::memcpy(packed.data() + i * kArrayStrideBytes,
          (std::byte const*)data + i * elementBytes, elementBytes);
      program.SetConstant(uniform.constant, packed);
    }

    void SetTexture(int varIndex, GpuTexture const* texture) {
      if (varIndex < 0 || varIndex >= (int)uniforms.size())
        return;
      Uniform const& uniform = uniforms[varIndex];
      if (uniform.textureSlot < 0) {
        WarnOnce(uniform, "is a value, not a texture");
        return;
      }
      TextureBinding& binding = textures[uniform.textureSlot];
      binding.id = texture ? texture->id : 0;
      binding.samplerSlot = uniform.samplerSlot;
    }
  };

  ProgramObject ProgramObject_Load(
    String const& vertPath,
    String const& fragPath)
  {
    String programPath = vertPath + "?" + fragPath;
    if (GetProgramCache().get(programPath))
      return GetProgramCache()[programPath];

    std::span<std::byte const> vs = ShaderRegistry_Vertex(vertPath);
    if (vs.empty())
      Log_Critical("Shader: no vertex shader was compiled for vertex/" + vertPath +
        " (Design/ADR/ADR-008)");
    std::span<std::byte const> ps = ShaderRegistry_Pixel(fragPath);
    if (ps.empty())
      Log_Critical("Shader: no pixel shader was compiled for fragment/" + fragPath +
        " (Design/ADR/ADR-008)");

    ProgramObject self = new ProgramObjectT;
    std::string const name(programPath.c_str());
    Neuron::Program::Desc desc;
    desc.vertexShader = vs;
    desc.pixelShader = ps;
    desc.name = name;
    self->program = Renderer_Device().CreateProgram(desc);
    self->vertPath = vertPath;
    self->fragPath = fragPath;
    self->path = programPath;
    self->CacheWVP();
    GetProgramCache()[programPath] = self;
    return self;
  }

  struct ShaderImpl : public ShaderT {
    ProgramObject program;

    ~ShaderImpl() {
      if (gActiveShader == this)
        gActiveShader = nullptr;
    }

    /* The shaders name their inputs and outputs by register, so there is
       nothing to bind or link (Design/ADR/ADR-008). */
    void BindInput(size_t, char const*) {}
    void BindOutput(size_t, char const*) {}

    void BindMatrices(
      Matrix const& world,
      Matrix const& view,
      Matrix const& proj,
      Matrix const& worldIT,
      Matrix const& WVP)
    {
      if (program->mWorld >= 0)
        SetMatrix(program->mWorld, &world);
      if (program->mView >= 0)
        SetMatrix(program->mView, &view);
      if (program->mProj >= 0)
        SetMatrix(program->mProj, &proj);
      if (program->mWorldIT >= 0)
        SetMatrix(program->mWorldIT, &worldIT);
      if (program->mWVP >= 0)
        SetMatrix(program->mWVP, &WVP);
    }

    bool Create(String const&, String const&) {
      Log_Error("Shader: shaders are compiled into lt.dll, not from source (Design/ADR/ADR-008)");
      return false;
    }

    int GetUniformLocation(char const* name) {
      return program->GetUniformLocation(name, true);
    }

    int QueryUniformLocation(char const* name) {
      return program->GetUniformLocation(name, false);
    }

    void PrintLogs() const {
      std::cout << ">>> Program " << program->path.c_str()
        << ": compiled at build time (Design/ADR/ADR-008)\n\n";
    }

    void Relink() {}

    ShaderT& SetCubeMap(char const* name, CubeMap const& cubeMap) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetCubeMap(varIndex, cubeMap);
    }

    ShaderT& SetCubeMap(int varIndex, CubeMap const& cubeMap) {
      Use();
      program->SetTexture(varIndex, cubeMap ? CubeMap_GetGpu(*cubeMap) : nullptr);
      return *this;
    }

    ShaderT& SetFloat(char const* name, float f) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetFloat(varIndex, f);
    }

    ShaderT& SetFloat(int varIndex, float f) {
      Use();
      program->SetConstant(varIndex, &f, sizeof(f));
      return *this;
    }

    ShaderT& SetFloatArray(char const* name, float const* data, size_t size) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetFloatArray(varIndex, data, size);
    }

    ShaderT& SetFloatArray(int varIndex, float const* data, size_t size) {
      Use();
      program->SetArray(varIndex, data, size, sizeof(float));
      return *this;
    }

    ShaderT& SetFloat2(char const* name, V2 const& v) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetFloat2(varIndex, v);
    }

    ShaderT& SetFloat2(int varIndex, V2 const& v) {
      Use();
      float const values[] = {v.x, v.y};
      program->SetConstant(varIndex, values, sizeof(values));
      return *this;
    }

    ShaderT& SetFloat3(char const* name, V3 const& v) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetFloat3(varIndex, v);
    }

    ShaderT& SetFloat3(int varIndex, V3 const& v) {
      Use();
      float const values[] = {v.x, v.y, v.z};
      program->SetConstant(varIndex, values, sizeof(values));
      return *this;
    }

    ShaderT& SetFloat4(char const* name, V4 const& v) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetFloat4(varIndex, v);
    }

    ShaderT& SetFloat4(int varIndex, V4 const& v) {
      Use();
      float const values[] = {v.x, v.y, v.z, v.w};
      program->SetConstant(varIndex, values, sizeof(values));
      return *this;
    }

    ShaderT& SetFloat3Array(char const* name, V3 const* data, size_t size) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetFloat3Array(varIndex, data, size);
    }

    ShaderT& SetFloat3Array(int varIndex, V3 const* data, size_t size) {
      Use();
      std::vector<float> values(3 * size);
      for (size_t i = 0; i < size; ++i) {
        values[3 * i + 0] = data[i].x;
        values[3 * i + 1] = data[i].y;
        values[3 * i + 2] = data[i].z;
      }
      program->SetArray(varIndex, values.data(), size, 3 * sizeof(float));
      return *this;
    }

    ShaderT& SetFloat4Array(char const* name, V4 const* data, size_t size) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetFloat4Array(varIndex, data, size);
    }

    ShaderT& SetFloat4Array(int varIndex, V4 const* data, size_t size) {
      Use();
      std::vector<float> values(4 * size);
      for (size_t i = 0; i < size; ++i) {
        values[4 * i + 0] = data[i].x;
        values[4 * i + 1] = data[i].y;
        values[4 * i + 2] = data[i].z;
        values[4 * i + 3] = data[i].w;
      }
      program->SetArray(varIndex, values.data(), size, 4 * sizeof(float));
      return *this;
    }

    ShaderT& SetMatrix(char const* name, Matrix const* m) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetMatrix(varIndex, m);
    }

    ShaderT& SetMatrix(int varIndex, Matrix const* m) {
      /* Its columns first, as GL took it, which HLSL's column-major float4x4
         reads the same way. */
      Use();
      program->SetConstant(varIndex, &(m->e[0]), sizeof(m->e));
      return *this;
    }

    ShaderT& SetInt(char const* name, int i) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetInt(varIndex, i);
    }

    ShaderT& SetInt(int varIndex, int i) {
      Use();
      program->SetConstant(varIndex, &i, sizeof(i));
      return *this;
    }

    ShaderT& SetTexture2D(char const* name, Texture2D const& t) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetTexture2D(varIndex, t);
    }

    ShaderT& SetTexture2D(int varIndex, Texture2D const& t) {
      Use();
      program->SetTexture(varIndex, t ? Texture2D_GetGpu(*t) : nullptr);
      return *this;
    }

    ShaderT& SetTexture3D(char const* name, Texture3D const& t) {
      int varIndex = GetUniformLocation(name);
      if (varIndex < 0)
        return *this;
      return SetTexture3D(varIndex, t);
    }

    ShaderT& SetTexture3D(int varIndex, Texture3D const& t) {
      Use();
      program->SetTexture(varIndex, t ? Texture3D_GetGpu(*t) : nullptr);
      return *this;
    }

    void Use() {
      gActiveProgram = program.t;
      gActiveShader = this;
    }
  };
}

bool Shader_BindActive(
  Neuron::DrawContext& context,
  std::span<Neuron::Texture const* const> targets)
{
  ProgramObjectT* program = gActiveProgram;
  if (!program || !program->program)
    return false;

  context.SetProgram(program->program);
  for (uint slot = 0; slot < kTextureUnits; ++slot) {
    TextureBinding const& binding = program->textures[slot];
    GpuTexture* texture = binding.id ? GpuTexture_Find(binding.id) : nullptr;
    Neuron::Texture const* bound = texture && texture->texture ? &texture->texture : nullptr;
    for (size_t i = 0; bound && i < targets.size(); ++i) {
      if (targets[i] == bound) {
        bound = nullptr;
        static bool warned = false;
        if (!warned) {
          warned = true;
          Log_Warning(Stringize() | program->Label() | " reads a texture it draws into, which GL "
            "left undefined; it reads nothing there");
        }
      }
    }
    context.SetTexture(slot, bound);
    if (bound && binding.samplerSlot >= 0)
      context.SetSampler(binding.samplerSlot, texture->sampler);
  }
  return true;
}

DefineFunction(Shader_Create) {
  Reference<ShaderImpl> self = new ShaderImpl;
  self->program = ProgramObject_Load(args.vsPath, args.fsPath);
  LTE_ASSERT(self->program);
  return self;
}

ShaderT* Shader_GetActive() {
  return gActiveShader;
}

void Shader_UseFixedFunction() {
  gActiveProgram = nullptr;
  gActiveShader = nullptr;
}
