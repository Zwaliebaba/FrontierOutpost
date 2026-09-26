#include "Colony.h"

#include "Materials.h"
#include "ShadingModels.h"
#include "Generators.h"

#include "CubeMap.h"
#include "DrawState.h"
#include "Location.h"
#include "Matrix.h"
#include "Meshes.h"
#include "Model.h"
#include "ShaderInstance.h"
#include "Texture2D.h"

/* How a Colony is drawn: the client's half of Colony.cpp (ADR-016). GenerateForests and
   GenerateCity came with it; nothing has called them since the original. */

float TreeDensity(V2 const& uv) {
  float h = TerrainFn(uv);
  return h < 0 ? 0.0f : (1.0f - h) * Fractal(WorleyNoise2D, uv * 3.0f, 8, 2.0f);
}

float OcclusionFn(V2 const& uv) {
  return 1.0f;
}

void GenerateForests(Object const& container) {
  ShaderInstance shader = ShaderInstance_Create("billboard_axis.jsl", "tree.jsl");
  (*shader)
    (RenderStateSwitch_BlendModeAlpha)
    (RenderStateSwitch_ZWritableOff)
    ("atmoDensity", 50.0f)
    ("atmoTint", V3(1, 0.5f, 0.1f))
    ("axis", V3(0, 1, 0))
    ("size", 2.0f * V2(1, 2))
    ("texture", Texture_LoadFrom(Location_Texture("tree.png")));
  DrawState_Link(shader);

  const float kTreeRegion = 1.0f;
  const float kGridSize = kTreeRegion / 10.f;

  for (float x = -kTreeRegion; x < kTreeRegion; x += kGridSize)
  for (float y = -kTreeRegion; y < kTreeRegion; y += kGridSize) {
    V2 uv(x, y);
    if (Length(uv) > 1.0f)
      continue;

    Mesh m = Mesh_Create();
    for (uint i = 0; i < kTrees; ++i) {
      V2 uvp = uv + RandV2(0, kGridSize);
      if (Rand() > TreeDensity(uvp))
        continue;

      V3 p = SpatialFn(uvp) + V3(0, 1, 0);
      m->AddMesh(Mesh_Billboard(), Matrix::Translation(p));
    }

    container->AddInterior(
      Object_Static((Renderable)Model_Create()->Add(m, shader)));
  }
}

void GenerateCity(Object const& container) {
  Mesh m = Mesh_Create();
  Mesh box = Mesh_Box(2, true);
  box->SetU(1);
  V2 center = RandV2(-0.75f, 0.75f);
  for (uint i = 0; i < 100; ++i) {
    V2 uv = center + 0.25f * Squared(Rand()) * Polar(RandAngle());
    V3 p = SpatialFn(uv);
    V3 s = 10 * V3(Rand(1, 2), 20 * Squared(Rand()), Rand(1, 2));
    m->AddMesh(box, Matrix::Translation(p) * Matrix::Scale(s));
  }

  container->AddInterior(
    Object_Static((Renderable)Model_Create()->Add(m, Material_Metal())));
}

namespace {
  struct ColonyVisual : public RenderableT {
    DERIVED_TYPE(ColonyVisual, RenderableT)

    Generic<CubeMap> envMap;
    Generic<CubeMap> envMapLF;
    Renderable interior;

    ColonyVisual(Colony* self) {
      this->envMap = Generator_PlanetSkybox(self->planet);
      this->envMapLF = Generator_Blur(envMap, 0.1f, 1024, 512);
    }

    /* Drawn through the Colony's interior, not as a renderable of its own. */
    void Render(DrawState*) const {}
  };

  ColonyVisual* VisualOf(ObjectT* object) {
    return (ColonyVisual*)((Colony*)object)->visual.t;
  }

  void BeginDrawInterior(ObjectT* self, DrawState* state) {
    self->GetContainer()->BeginDrawInterior(state);
    DrawState_Push("fogDensity", 0.1f);
    // state->envMap.push(envMap());
    // state->envMapLF.push(envMapLF());
  }

  void EndDrawInterior(ObjectT* self, DrawState* state) {
    // state->envMap.pop();
    // state->envMapLF.pop();
    DrawState_Pop("fogDensity");
    self->GetContainer()->EndDrawInterior(state);
  }

  void OnDrawInterior(ObjectT* self, DrawState* state) {
    Renderable& interior = VisualOf(self)->interior;
    if (!interior) {
      Mesh terrainMesh = Mesh_Plane(
        V3(-1, 0, -1),
        V3(2, 0, 0),
        V3(0, 0, 2), kTerrainQuality, kTerrainQuality);

      for (size_t i = 0; i < terrainMesh->vertices.size(); ++i) {
        Vertex& v = terrainMesh->vertices[i];
        V2 uv = v.p.GetXZ();
        v.p = SpatialFn(uv);
        v.u = OcclusionFn(uv);
        v.v = 0;
      }

      terrainMesh->ComputeNormals();

      Mesh waterMesh = Mesh_Plane(
        V3(-1, 0, -1),
        V3(2, 0, 0),
        V3(0, 0, 2), 100, 100);

      for (size_t i = 0; i < waterMesh->vertices.size(); ++i) {
        Vertex& v = waterMesh->vertices[i];
        v.p.y += 5.0f;
        v.p = Normalize(v.p);
        v.p *= kTerrainScale;
      }

      interior = (Renderable)Model_Create()
        ->Add(waterMesh, Material_Water())
        ->Add(terrainMesh, Material_Grass());
      //for (uint i = 0; i < 1; ++i) GenerateCity(this);
      // GenerateForests(this);
    }

    self->GetContainer()->OnDrawInterior(state);
    interior->Render(state);
  }

  void Create(ObjectT* object, Renderable& visual) {
    visual = new ColonyVisual((Colony*)object);
  }

  bool registered =
    (Game::RegisterVisual("Colony", Create),
     Game::RegisterDraw("Colony", Game::DrawPhase::BeginInterior, BeginDrawInterior),
     Game::RegisterDraw("Colony", Game::DrawPhase::Interior, OnDrawInterior),
     Game::RegisterDraw("Colony", Game::DrawPhase::EndInterior, EndDrawInterior), true);
}
