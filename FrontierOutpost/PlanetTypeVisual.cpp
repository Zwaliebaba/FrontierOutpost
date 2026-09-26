#include "PlanetType.h"

#include "Generators.h"

#include "CubeMap.h"
#include "DrawState.h"
#include "LteMath.h"
#include "Meshes.h"
#include "Model.h"
#include "RNG.h"
#include "ShaderInstance.h"
#include "Texture2D.h"

#include "Visual.h"

/* How a PlanetType looks: the client's half of PlanetType.cpp (ADR-016). Generate draws on its
   own RNG, seeded from the type's seed, as it did. */

const float kOuterScale = 1.025f;
const uint kMeshQuality = 50;

namespace {
  Renderable Generate(PlanetType const& type) {
    static Mesh planetMesh;
    static Mesh atmoMesh;
    static Mesh ringMesh;
    static Shader planetShader = Shader_Create("npm.jsl", "planet.jsl");
    static Shader atmoShader = Shader_Create("npm.jsl", "atmosphere.jsl");
    static Shader ringShader = Shader_Create("npm.jsl", "planetring.jsl");

    if (!planetMesh) {
      planetMesh = Mesh_BoxSphere(kMeshQuality, true)->SetU(1);
      atmoMesh = Mesh_BoxSphere(kMeshQuality, true)
        ->Scale(kOuterScale)
        ->ReverseWinding()
        ->SetU(1);
      ringMesh = Mesh_Quad()
        ->Rotate(V3(0, kPi2, 0))
        ->Scale(3.0f);
    }

    uint seed = type.GetSeed();
    RNG rg = RNG_MTG(seed);
    Model model = Model_Create();

    /* Planet. */ {
      ShaderInstance planetShaderInstance = ShaderInstance_Create(planetShader);
      float heightMult = 1;
      float oceanLevel = Pow(rg->GetFloat(), 1.5f);

      (*planetShaderInstance)
        ("atmoDensity", type.atmoDensity)
        ("atmoTint", type.atmoTint)
        ("cloudLevel", type.cloudLevel)
        ("color1", type.color1)
        ("color2", rg->GetV3(0.5f, 0.75f))
        ("color3", rg->GetV3(0.5f, 0.75f))
        ("color4", type.color2)
        ("colorSeed", rg->GetFloat(1, 1000))
        ("heightMult", heightMult)
        ("oceanLevel", oceanLevel)
        ("planetMap",
          DiskCached(Generator_PlanetSurface(seed), Stringize() | "planetsurface_" | seed))
        ("wavelength", type.wavelength);
      DrawState_Link(planetShaderInstance);
      model->Add(planetMesh, planetShaderInstance);
    }

    /* Atmosphere. */ {
      ShaderInstance atmoShaderInstance = ShaderInstance_Create(atmoShader);
      (*atmoShaderInstance)
        (RenderStateSwitch_BlendModeAdditive)
        ("atmoDensity", type.atmoDensity)
        ("atmoTint", type.atmoTint)
        ("wavelength", type.wavelength);
      DrawState_Link(atmoShaderInstance);
      model->Add(atmoMesh, atmoShaderInstance, false);
    }

    /* Rings. */ {
      static Shader generate = Shader_Create("identity.jsl", "gen/planetring.jsl");
      (*generate)("seed", rg->GetFloat());

      Texture2D ringTexture = Texture_Create(1024, 1, TextureFormat::R32F);
      Texture_Generate(ringTexture, generate);

      ShaderInstance ringShaderInstance = ShaderInstance_Create(ringShader);
      (*ringShaderInstance)
        (RenderStateSwitch_BlendModeAlpha)
        (RenderStateSwitch_CullModeDisabled)
        ("rings", ringTexture);
      DrawState_Link(ringShaderInstance);
      model->Add(ringMesh, ringShaderInstance, false);
    }

    return model;
  }

  void Create(ItemT* item, uint32, Renderable& renderable) {
    renderable = Generate(*(PlanetType*)item);
  }

  bool registered = (Game::RegisterItemVisual("PlanetType", Create), true);
}
