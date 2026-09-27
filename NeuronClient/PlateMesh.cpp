#include "PlateMesh.h"

#include "Matrix.h"
#include "Meshes.h"
#include "StackFrame.h"
#include "Warp.h"

#include "Array.h"
#include "ProgramLog.h"
#include "Renderer.h"
#include "Shader.h"
#include "Texture2D.h"
#include "Timer.h"

namespace {
  AutoClass(Plate,
    V3, center,
    V3, scale,
    V3, rotation,
    float, bevel)
    Plate() {}
  };

  AutoClassDerived(PlateMeshImpl, PlateMeshT,
    Vector<Plate>, plates,
    Vector<PlateMesh>, children,
    Vector<Matrix>, childTransforms,
    Vector<Warp>, warps,
    uint, quality)
    DERIVED_TYPE_EX(PlateMeshImpl)

    PlateMeshImpl() {}

    void Add(
      V3 const& center,
      V3 const& scale,
      V3 const& rotation,
      float bevel)
    {
      plates.push(Plate(center, scale, rotation, bevel));
    }

    void Add(Warp const& warp) {
      warps.push(warp);
    }

    void Add(
      PlateMesh const& pm,
      V3 const& offset = 0,
      V3 const& scale = 1)
    {
      children.push(pm);
      childTransforms.push(
        Matrix::Translation(offset) *
        Matrix::Scale(scale));
    }

    Mesh GetMesh() const {
      Mesh self = Mesh_Create();

      for (size_t i = 0; i < plates.size(); ++i) {
        Plate const& plate = plates[i];
        self->AddMesh(
          Mesh_BoxRounded(quality, plate.bevel),
          Matrix::Translation(plate.center) *
          Matrix::RotationYPR(plate.rotation) *
          Matrix::Scale(plate.scale));
      }

      for (size_t i = 0; i < children.size(); ++i)
        self->AddMesh(children[i]->GetMesh(), childTransforms[i]);

      for (size_t i = 0; i < self->vertices.size(); ++i) {
        V3& p = self->vertices[i].p;
        V3 delta = 0;
        for (size_t j = 0; j < warps.size(); ++j)
          delta += warps[j]->GetDelta(p);
        p += delta;
      }

      self->ComputeNormals();
      return self;
    }

    void ReflectX() {
      for (size_t i = 0; i < plates.size(); ++i) {
        plates[i].center.x *= -1;
        plates[i].rotation.z *= -1;
      }
    }

    void ReflectY() {
      for (size_t i = 0; i < plates.size(); ++i) {
        plates[i].center.y *= -1;
        plates[i].rotation.y *= -1;
      }
    }

    void ReflectZ() {
      for (size_t i = 0; i < plates.size(); ++i) {
        plates[i].center.z *= -1;
        plates[i].rotation.x *= -1;
      }
    }
  };
}

DefineFunction(PlateMesh_Create) {
  Reference<PlateMeshImpl> self = new PlateMeshImpl;
  self->quality = args.quality;
  return self;
}

DefineFunction(Mesh_ComputeOcclusion) {
  AUTO_FRAME;
  Mesh const& m = args.mesh;
  Timer timer;

  Vector<V4> points;
  Vector<V4> normals;

  /* Fill surfel buffers. */
  for (size_t i = 0; i < m->indices.size(); i += 3) {
    Vertex const& v0 = m->vertices[m->indices[i + 0]];
    Vertex const& v1 = m->vertices[m->indices[i + 1]];
    Vertex const& v2 = m->vertices[m->indices[i + 2]];

    V3 normal = Cross(v2.p - v0.p, v1.p - v0.p);
    float area = 0.5f * Length(normal) / kPi;
    points.push(V4((v0.p + v1.p + v2.p) / 3.0f, area));
    normals.push(V4(Normalize(normal), 0.0f));
  }

  int sDim = (int)Ceil(Sqrt((float)points.size()));

  while ((int)points.size() < sDim * sDim) {
    points.push(V4(0));
    normals.push(V4(0));
  }

  /* The surfels go to the shader's constant buffer a band at a time, at most
     the MAX_SURFELS of Shaders/ComputeOcclusionPS.hlsl. */
  const int kMaxSurfels = 2040;
  if (sDim > kMaxSurfels) {
    Log_Critical(Stringize() | "PlateMesh: a surfel row of " | sDim |
      " is more than Shaders/ComputeOcclusionPS.hlsl holds");
  }

  /* Fill vertex buffers. */
  Vector<V4> vPoints;
  Vector<V4> vNormals;
  for (size_t i = 0; i < m->vertices.size(); ++i) {
    vPoints.push(V4(m->vertices[i].p, 0));
    vNormals.push(V4(m->vertices[i].n, 0));
  }

  int vDim = (int)Ceil(Sqrt((float)vPoints.size()));

  while ((int)vPoints.size() < vDim * vDim) {
    vPoints.push(V4(0));
    vNormals.push(V4(0));
  }

  Texture2D vPointBuffer =
    Texture_Create(vDim, vDim, TextureFormat::RGBA32F, vPoints.data());
  Texture2D vNormalBuffer =
    Texture_Create(vDim, vDim, TextureFormat::RGBA32F, vNormals.data());

  /* GPU computation. */
  Texture2D occlusionBuffer =
    Texture_Create(vDim, vDim, TextureFormat::R32F);
  static Shader shader = Shader_Create("identity.jsl", "compute/occlusion.jsl");

  /* Every vertex sums over every surfel: one draw of that on a big hull ran
     past Windows' GPU timeout (TDR) and lost the device. So the surfel rows
     are split into bands small enough to finish quickly, and no larger than
     the constant buffer holds, each drawn and submitted on its own, and the
     sums added up with additive blending. The CPU waits only every
     kBandsPerWait bands, so that the upload ring recycles its pages. */
  const double kInteractionsPerDraw = 1 << 25;
  const uint kBandsPerWait = 16;
  int rowsPerDraw = (int)(kInteractionsPerDraw / ((double)vDim * vDim * sDim));
  rowsPerDraw = Max(1, Min(Min(sDim, kMaxSurfels / sDim), rowsPerDraw));

  uint bands = 0;
  occlusionBuffer->Bind(0);
  Renderer_Clear(V4(0));
  Renderer_PushBlendMode(BlendMode::Additive);
  for (int rowBegin = 0; rowBegin < sDim; rowBegin += rowsPerDraw) {
    ++bands;
    int const rowEnd = Min(sDim, rowBegin + rowsPerDraw);
    size_t const first = (size_t)rowBegin * sDim;
    size_t const count = (size_t)(rowEnd - rowBegin) * sDim;
    (*shader)
      ("sDim", sDim)
      ("sRowBegin", rowBegin)
      ("sRowEnd", rowEnd)
      ("vPointBuffer", vPointBuffer)
      ("vNormalBuffer", vNormalBuffer);
    shader->SetFloat4Array("sPoint", points.data() + first, count);
    shader->SetFloat4Array("sNormal", normals.data() + first, count);
    Renderer_SetShader(*shader);
    Renderer_DrawFSQ();
    if (bands % kBandsPerWait == 0)
      Renderer_Finish();
    else
      Renderer_Flush();
  }
  Renderer_PopBlendMode();
  occlusionBuffer->Unbind();

  /* Write result to mesh. */
  Array<float> result(vDim * vDim);
  occlusionBuffer->GetData(result.data());
  for (size_t i = 0; i < m->vertices.size(); ++i)
    m->vertices[i].u = Exp(-Pow(Abs(result[i]), 0.75f));
  m->version++;

  /* How long the bake took, GPU included, since the readback waits for every
     band (plan M1, Design/ShaderPerformance-plan.md). */
  Log_Message(Stringize() | "PlateMesh: occlusion of " |
    (uint)m->vertices.size() | " vertices over " | (uint)(m->indices.size() / 3) |
    " surfels in " | bands | " bands took " |
    (uint)(1000.0f * timer.GetElapsed()) | " ms");
}
