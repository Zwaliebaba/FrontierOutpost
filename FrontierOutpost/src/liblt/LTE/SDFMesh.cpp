#include "SDFMesh.h"
#include "Array.h"
#include "Bound.h"
#include "Geometry.h"
#include "Job.h"
#include "Math.h"
#include "Matrix.h"
#include "Mesh.h"
#include "ProgramLog.h"
#include "Ray.h"
#include "Renderer.h"
#include "RendererCore.h"
#include "SDF.h"
#include "ShaderRegistry.h"
#include "Texture3D.h"
#include "Thread.h"
#include "Vector.h"

#include "Module/Scheduler.h"

#include "Volume/Array3D.h"

#include "LTE/Debug.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

/* Field Texture -> (LOD Level -> MC) -> Ambient Occlusion
 *
 * On the GPU, by three compute shaders (Design/ADR/ADR-009): Shaders/GenFieldCS.hlsl interprets the
 * SDF, encoded as instructions (LTE/SDF.h), for each voxel of the field; GenFieldcopyCS.hlsl
 * resamples the field into each level of detail's grid, which is read back for the marching cubes
 * on the CPU; GenFieldocclusionCS.hlsl gives each vertex of a level its ambient occlusion. */

const uint kMaxQuality = 128;
const uint kMaxResolution = 256;
const uint kMinResolution = 4;

/* TODO - Remove this number : just like mips, should create every level until
 *        resolution drops below some point. */
const uint kLodLevels = 8;
const float kLodDownsampleFactor = 0.666f;
const float kLodFactor = 1.0f;

const uint kCollisionMeshResolution = 32;
const uint kOcclusionSamples = 2048;
const float kBaseOcclusionRadius = 0.3f;
const bool kPreload = false;

/* What Shaders/GenFieldCS.hlsl holds of an SDF: its MAX_ macros. */
const size_t kMaxInstructions = 64;
const uint kMaxValues = 16;
const uint kMaxPoints = 8;

typedef V3T<uint> SV3;

namespace {
  Neuron::Program MakeProgram(char const* name) {
    std::span<std::byte const> bytecode = ShaderRegistry_Compute(name);
    if (bytecode.empty())
      Log_Critical(String("SDFMesh: no compute shader was compiled for ") + name +
        " (Design/ADR/ADR-008)");
    Neuron::Program::Desc desc;
    desc.computeShader = bytecode;
    desc.name = name;
    return Renderer_Device().CreateProgram(desc);
  }

  /* Made when first used, and shared by every mesh, so each dispatch sets all
     the constants it reads. */
  Neuron::Program& FieldProgram() {
    static Neuron::Program program = MakeProgram("gen/field.jsl");
    return program;
  }

  Neuron::Program& CopyProgram() {
    static Neuron::Program program = MakeProgram("gen/fieldcopy.jsl");
    return program;
  }

  Neuron::Program& OcclusionProgram() {
    static Neuron::Program program = MakeProgram("gen/fieldocclusion.jsl");
    return program;
  }

  template <class T>
  void SetConstant(Neuron::Program& program, char const* name, T const& value) {
    program.SetConstant(name, std::as_bytes(std::span<T const>(&value, 1)));
  }

  void SetConstant(Neuron::Program& program, char const* name, SV3 const& value) {
    std::uint32_t const values[3] = {value.x, value.y, value.z};
    program.SetConstant(name, std::as_bytes(std::span(values)));
  }

  void SetConstant(Neuron::Program& program, char const* name, V3 const& value) {
    float const values[3] = {value.x, value.y, value.z};
    program.SetConstant(name, std::as_bytes(std::span(values)));
  }

  /* Thread groups of _size that cover _count. */
  uint Groups(uint count, uint size) {
    return (count + size - 1) / size;
  }

  Neuron::Texture MakeTexture(
    Neuron::TextureDimension dimension,
    Neuron::TextureFormat format,
    uint width,
    uint height,
    uint depth,
    char const* name)
  {
    Neuron::Texture::Desc desc;
    desc.dimension = dimension;
    desc.format = format;
    desc.widthPixels = width;
    desc.heightPixels = height;
    desc.depthPixels = depth;
    desc.mipLevels = 1;
    desc.name = name;
    return Renderer_Device().CreateTexture(desc);
  }

  struct SDFMesh;
  struct SDFMeshLevel {
    Mesh mesh;
    SV3 res;
    SDFMeshLevel() {}
    SDFMeshLevel(SV3 const& res) :
      res(res)
      {}
  };
    
  Job Job_GenerateFieldTexture(SDFMesh* parent, SV3 const& res);
  Job Job_GenerateLodLevel(SDFMesh* p, SDFMeshLevel& level);
  Job Job_GenerateOcclusion(SDFMesh* p, SDFMeshLevel& level);

  AutoClassDerived(SDFMesh, GeometryT,
    SDF, function,
    V3, resolutionMult,
    float, occlusionRadiusMult,
    float, tileMult,
    bool, eliminateFloaters)
    Vector<SDFMeshLevel> levels;
    SDFMeshLevel collisionLevel;
    Texture3D fieldTexture;
    Bound3 bound;
    bool isLoaded;

    DERIVED_TYPE_EX(SDFMesh)

    SDFMesh() {}
 
    DefineInitializer {
      this->collisionLevel = SDFMeshLevel(kCollisionMeshResolution);
      this->bound = function->GetBound().GetExpanded(1.05f);
      this->isLoaded = false;

      if (kPreload)
        Load();
    }

    Mesh GetMesh(size_t lodLevel) const {
      if (levels.empty())
        return nullptr;
      size_t index = Min(levels.size() - 1, lodLevel);
      while (!levels[index].mesh && index + 1 < levels.size())
        index++;
      return levels[index].mesh;
    }

    void Draw() const {
      Mutable(this)->Load();

      Matrix const& world = Renderer_GetWorldMatrix();
      Bound3 worldBound = bound.GetTransformed(world);
      float radius = worldBound.GetRadius();
      float distance = Length(worldBound.GetCenter());

      float lod = kLodFactor * Max(0.0f, distance / radius - 1.0f);
      int lodLevel = (int)(Log(1.0 + lod) / Log(1.0f / kLodDownsampleFactor));

      Mesh currentMesh;
      while (!currentMesh) {
        currentMesh = GetMesh(lodLevel);
        if (!currentMesh)
          Scheduler_Get()->Flush();
      }

      currentMesh->Draw();
    }

    Bound3 GetBound() const {
      return bound;
    }

    Mesh GetCollisionMesh() const {
      Mesh const& m = collisionLevel.mesh;
      return m ? m->Clone() : nullptr;
    }

    short GetVersion() const {
      short versionSum = 0;
      for (size_t i = 0; i < levels.size(); ++i)
        versionSum = (short)(versionSum + (levels[i].mesh ? levels[i].mesh->GetVersion() : 0));
      return versionSum;
    }

    bool Intersects(
      Ray const& ray,
      float* tOut,
      V3* normalOut,
      V2* uvOut) const
    {
      float near, far;
      if (!bound.Intersects(ray.origin, V3(1) / ray.direction, near, far))
        return false;

      const size_t kSubdivisions = 64;
      const size_t kRefinements = 8;
      float rayStep = (far - near) / kSubdivisions;
      float t = near;
      for (size_t i = 0; i < kSubdivisions; ++i) {
        t += rayStep;
        float thisField = function->Evaluate(ray(t));
        if (thisField <= 0) {
          for (size_t j = 0; j < kRefinements; ++j) {
            rayStep /= 2.0f;
            t += thisField <= 0 ? -rayStep : rayStep;
          }

          if (tOut)
            *tOut = t;
          if (normalOut)
            *normalOut = Normalize(function->Gradient(ray(t)));
          return true;
        }
      }
      return false;
    }

    bool IsFullyLoaded() const {
      if (!collisionLevel.mesh)
        return false;
      for (size_t i = 0; i < levels.size(); ++i)
        if (!levels[i].mesh)
          return false;
      return true;
    }

    void Load() {
      if (!isLoaded) {
        V3 extent = bound.GetSideLengths();
        V3 vertexDensity = resolutionMult * extent / extent.GetGeometricAverage();
        SV3 fullRes = (float)kMaxQuality * vertexDensity;
        if (((V3)fullRes).GetGeometricAverage() > kMaxResolution)
          fullRes = (SV3)((V3)fullRes * (kMaxResolution / ((V3)fullRes).GetGeometricAverage()));

        SV3 levelResolution = fullRes;
        for (uint i = 0; i < kLodLevels; ++i) {
          levels.push(SDFMeshLevel(Max(levelResolution, SV3(kMinResolution))));
          levelResolution = V3(levelResolution) * kLodDownsampleFactor;
        }

        Scheduler_Add(Job_GenerateFieldTexture(this, fullRes), false);
        Scheduler_Add(Job_GenerateLodLevel(this, collisionLevel), false);

        for (size_t i = 0; i < levels.size(); ++i) {
          Scheduler_Add(Job_GenerateLodLevel(this, levels[i]), false);
          Scheduler_Add(Job_GenerateOcclusion(this, levels[i]), false);
        }

        isLoaded = true;
      }
    }

    void Unload() {
      fieldTexture = nullptr;
    }

    V3 Sample() const {
      Mesh m = GetMesh(0);
      return m ? m->Sample() : 0;
    }
  };

  struct GenerateFieldTexture : public JobT {
    SDFMesh* parent;
    Texture3D output;
    SV3 res;
    uint z;
    SDFProgram program;

    GenerateFieldTexture(SDFMesh* parent, SV3 const& res) :
      parent(parent),
      res(res),
      z(0)
      {}

    char const* GetName() const {
      return "SDFMesh::Field Texture";
    }

    uint GetMemoryUsage() const {
      return 16 * res.GetProduct();
    }

    float GetPriority() const {
      return 1;
    }

    bool IsFinished() const {
      return z >= res.z;
    }

    void OnBegin() {
      output = Texture3D_Create(res.x, res.y, res.z, TextureFormat::R32F);
      parent->function->Encode(program);
      if (program.Instructions() > kMaxInstructions ||
          program.maxValues > kMaxValues ||
          program.maxPoints > kMaxPoints)
      {
        Log_Critical(Stringize() | "SDFMesh: an SDF of " | (uint)program.Instructions() |
          " instructions, " | program.maxValues | " values and " | program.maxPoints |
          " points deep is more than Shaders/GenFieldCS.hlsl holds");
      }
    }

    /* How long the interpreter took (Design/ADR/ADR-009): the scheduler times
       each run of a job with the GPU's work finished. */
    void OnEnd() {
      parent->fieldTexture = output;
      Log_Message(Stringize() | "SDFMesh: a field of " | res.x | "x" | res.y | "x" |
        res.z | " voxels took " | (uint)(1000.0f * totalTime) | " ms");
    }

    /* jobSize slices at a time, as the render passes before did. */
    void OnRun(uint jobSize) {
      uint const slices = Min(Max(jobSize, 1u), res.z - z);
      Neuron::Program& field = FieldProgram();
      field.SetConstant("instructions", std::as_bytes(std::span(program.words)));
      SetConstant(field, "instructionCount", (std::uint32_t)program.Instructions());
      SetConstant(field, "origin", parent->bound.lower);
      SetConstant(field, "extent", parent->bound.GetSideLengths());
      SetConstant(field, "resolution", res);
      SetConstant(field, "firstSlice", (std::uint32_t)z);

      Neuron::DrawContext& context = Renderer_Context();
      context.SetUnorderedTexture(0, &Texture3D_GetGpu(*output)->texture);
      context.Dispatch(field, Groups(res.x, 4), Groups(res.y, 4), Groups(slices, 4));
      context.SetUnorderedTexture(0, nullptr);
      z += slices;
    }
  };

  struct GenerateLodLevel : public JobT {
    SDFMesh* p;
    SDFMeshLevel& level;
    Mesh output;
    Array3DFloat grid;
    /* The level's grid on the GPU, and the ticket of its readback, 0 until
       it starts (Design/ADR/ADR-009). */
    Neuron::Texture texture;
    std::uint64_t read;
    bool polygonizing;

    struct Polygonize : public JobT {
      Reference<GenerateLodLevel> parent;
      Mesh output;

      Polygonize(Reference<GenerateLodLevel> const& parent) :
        parent(parent)
        {}

      char const* GetName() const {
        return "SDFMesh::Polygonize";
      }

      uint GetMemoryUsage() const {
        return 2 * parent->level.res.GetProduct() * sizeof(Vertex);
      }

      bool IsFinished() const {
        return output;
      }

      void OnEnd() {
        parent->output = output;
      }

      void OnRun(uint size) {
        output = Mesh_Volume(parent->grid, parent->p->bound);

        if (parent->p->eliminateFloaters) {
          Mesh m = output;
          output = m->ComputePrincipleComponent();
        }

        if (!output)
          output = Mesh_Create();
      }
    };

    GenerateLodLevel(SDFMesh* p, SDFMeshLevel& level) :
      p(p),
      level(level),
      grid(level.res.x, level.res.y, level.res.z),
      read(0),
      polygonizing(false)
      {}

    bool CanRun() const {
      return p->fieldTexture != nullptr && !polygonizing;
    }

    char const* GetName() const {
      return "SDFMesh::LOD Level";
    }

    uint GetMemoryUsage() const {
      return 16 * level.res.GetProduct();
    }

    float GetPriority() const {
      return Exp(-Pow((float)level.res.GetProduct(), 1.0f / 3.0f));
    }

    bool IsFinished() const {
      return output;
    }

    void OnBegin() {
      texture = MakeTexture(Neuron::TextureDimension::Texture3D, Neuron::TextureFormat::R32F,
        level.res.x, level.res.y, level.res.z, "SDFMesh level of detail");
    }

    void OnEnd() {
      level.mesh = output;
    }

    /* The first run resamples the field and starts reading the grid back; the
       runs after it take the grid once the GPU has copied it, and launch the
       contouring job. */
    void OnRun(uint jobSize) {
      Neuron::DrawContext& context = Renderer_Context();
      if (!read) {
        Neuron::Program& copy = CopyProgram();
        Texture3D const& field = p->fieldTexture;
        SetConstant(copy, "fieldResolution",
          SV3(field->GetWidth(), field->GetHeight(), field->GetDepth()));
        SetConstant(copy, "resolution", level.res);
        context.SetTexture(0, &Texture3D_GetGpu(*field)->texture);
        context.SetUnorderedTexture(0, &texture);
        context.Dispatch(copy,
          Groups(level.res.x, 4), Groups(level.res.y, 4), Groups(level.res.z, 4));
        context.SetUnorderedTexture(0, nullptr);
        context.SetTexture(0, nullptr);
        read = context.RequestRead(texture, 0, 0);
        context.Flush();
        return;
      }

      std::vector<std::byte> texels;
      if (!context.TakeRead(read, texels))
        return;
      size_t const bytes = grid.data.size() * sizeof(float);
      if (texels.size() == bytes)
        std::memcpy(grid.data.data(), texels.data(), bytes);
      else
        Log_Error("SDFMesh: a level of detail read back at another size than its grid");
      polygonizing = true;
      Scheduler_Add(new Polygonize(this), true);
    }
  };

  struct GenerateOcclusion : public JobT {
    SDFMesh* p;
    SDFMeshLevel& level;
    uint x;
    uint dim;
    uint samples;
    /* Each vertex's position and normal at texel i % dim, i / dim, the
       directions sampled, and the occlusion written for each vertex. */
    Neuron::Texture positionTexture;
    Neuron::Texture normalTexture;
    Neuron::Texture noiseTexture;
    Neuron::Texture occlusionTexture;
    std::uint64_t read;
    std::vector<std::byte> occlusion;
    bool done;

    GenerateOcclusion(SDFMesh* p, SDFMeshLevel& level) :
      p(p),
      level(level),
      x(0),
      read(0),
      done(false)
      {}

    bool CanRun() const {
      return level.mesh != nullptr;
    }

    char const* GetName() const {
      return "SDFMesh::Occlusion";
    }

    uint GetMemoryUsage() const {
      return 16 * level.res.GetProduct();
    }

    float GetPriority() const {
      return 0;
    }

    bool IsFinished() const {
      return done;
    }

    void OnBegin() {
      Mesh const& input = level.mesh;
      float radius = p->occlusionRadiusMult * kBaseOcclusionRadius;
      samples = (uint)Ceil(Sqrt((float)kOcclusionSamples));
      uint rootSamples = samples;
      samples *= samples;
      dim = Max((uint)Ceil(Sqrt((float)input->vertices.size())), 1u);

      /* Create the vertex position & normal buffers. */
      std::vector<V4> positionBuffer(dim * dim, V4(0));
      std::vector<V4> normalBuffer(dim * dim, V4(0));
      for (size_t i = 0; i < input->vertices.size(); ++i) {
        positionBuffer[i] = V4(input->vertices[i].p, 0);
        normalBuffer[i] = V4(input->vertices[i].n, 0);
      }

      Neuron::DrawContext& context = Renderer_Context();
      positionTexture = MakeTexture(Neuron::TextureDimension::Texture2D,
        Neuron::TextureFormat::Rgba32F, dim, dim, 1, "SDFMesh vertex positions");
      normalTexture = MakeTexture(Neuron::TextureDimension::Texture2D,
        Neuron::TextureFormat::Rgba32F, dim, dim, 1, "SDFMesh vertex normals");
      context.UpdateTexture(positionTexture, 0, 0, std::as_bytes(std::span(positionBuffer)));
      context.UpdateTexture(normalTexture, 0, 0, std::as_bytes(std::span(normalBuffer)));

      /* Create the sampling pattern buffer. */
      std::vector<V4> noiseBuffer(samples);
      uint bufferIndex = 0;
      for (uint i = 0; i < rootSamples; ++i) {
        float thisRadius = Sqrt((float)i / (float)(rootSamples - 1));
        for (uint j = 0; j < rootSamples; ++j) {
          float angle = kTau * (float)j / (float)(rootSamples - 1);
          float y = thisRadius * Cos(angle);
          float z = thisRadius * Sin(angle);
          float x = Sqrt(1.0f - thisRadius * thisRadius);
          x += 0.04f;
          float factor = -Log(1.0f - Rand());
          noiseBuffer[bufferIndex++] = V4((radius * factor) * V3(x, y, z), 0);
        }
      }

      noiseTexture = MakeTexture(Neuron::TextureDimension::Texture2D,
        Neuron::TextureFormat::Rgba32F, samples, 1, 1, "SDFMesh occlusion directions");
      context.UpdateTexture(noiseTexture, 0, 0, std::as_bytes(std::span(noiseBuffer)));
      occlusionTexture = MakeTexture(Neuron::TextureDimension::Texture3D,
        Neuron::TextureFormat::R32F, dim, dim, 1, "SDFMesh occlusion");
    }

    void OnEnd() {
      Mesh const& input = level.mesh;
      float const* values = (float const*)occlusion.data();
      size_t const count = occlusion.size() / sizeof(float);
      for (size_t i = 0; i < input->vertices.size() && i < count; ++i)
        input->vertices[i].u = values[i];

      input->version++;
    }

    /* jobSize columns at a time, as the render passes before did, then one
       readback of them all (Design/ADR/ADR-009). */
    void OnRun(uint jobSize) {
      Neuron::DrawContext& context = Renderer_Context();
      if (x < dim) {
        uint const columns = Min(Max(jobSize, 1u), dim - x);
        V3 extent = p->bound.GetSideLengths();
        Texture3D const& field = p->fieldTexture;
        Neuron::Program& program = OcclusionProgram();
        SetConstant(program, "samples", (std::uint32_t)samples);
        SetConstant(program, "dimension", (std::uint32_t)dim);
        SetConstant(program, "firstColumn", (std::uint32_t)x);
        SetConstant(program, "endColumn", (std::uint32_t)(x + columns));
        SetConstant(program, "fieldOrigin", p->bound.lower);
        SetConstant(program, "fieldExtent", extent);
        SetConstant(program, "fieldStep", extent / V3(level.res));
        SetConstant(program, "fieldResolution",
          SV3(field->GetWidth(), field->GetHeight(), field->GetDepth()));

        context.SetTexture(0, &positionTexture);
        context.SetTexture(1, &normalTexture);
        context.SetTexture(2, &noiseTexture);
        context.SetTexture(3, &Texture3D_GetGpu(*field)->texture);
        context.SetUnorderedTexture(0, &occlusionTexture);
        context.Dispatch(program, Groups(columns, 8), Groups(dim, 8), 1);
        context.SetUnorderedTexture(0, nullptr);
        for (uint slot = 0; slot < 4; ++slot)
          context.SetTexture(slot, nullptr);
        x += columns;

        if (x >= dim) {
          read = context.RequestRead(occlusionTexture, 0, 0);
          context.Flush();
        }
        return;
      }

      if (read && context.TakeRead(read, occlusion))
        done = true;
    }
  };

  Job Job_GenerateFieldTexture(SDFMesh* parent, SV3 const& res) {
    return new GenerateFieldTexture(parent, res);
  }

  Job Job_GenerateLodLevel(SDFMesh* p, SDFMeshLevel& level) {
    return new GenerateLodLevel(p, level);
  }

  Job Job_GenerateOcclusion(SDFMesh* p, SDFMeshLevel& level) {
    return new GenerateOcclusion(p, level);
  }
}

namespace LTE {
  Geometry SDFMesh_Create(
    SDF const& field,
    V3 const& resolutionMult,
    float occlusionRadiusMult,
    float tileMult,
    bool eliminateFloaters)
  {
    return new SDFMesh(
      field,
      resolutionMult,
      occlusionRadiusMult,
      tileMult,
      eliminateFloaters);
  }
}
