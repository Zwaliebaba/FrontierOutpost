#include "System.h"

#include "Messages.h"
#include "Renderables.h"
#include "Generators.h"

#include "CubeMap.h"
#include "DrawState.h"
#include "Keyboard.h"
#include "LteMath.h"
#include "RNG.h"
#include "Texture2D.h"
#include "Texture3D.h"
#include "View.h"

/* How a System is drawn: the client's half of System.cpp (ADR-016). The system's look is made
   from its seed, drawing on that seed's RNG in the order System::Initialize did. */

const uint kBaseStarCount = 100000;

const int kColorIterations = 4;
const int kColorPoints = 256;
const float kColorVariation = 0.02f;
const float kColorLacunarity = 0.6f;

namespace {
  V3 GenerateStarColor(RNG const& rng) {
    return V3(1.0f) - 0.5f * Log(rng->GetV3(0, 1)) * V3(1.0f, 0.5f, 1.0f);
  }

  struct SystemVisual : public RenderableT {
    DERIVED_TYPE(SystemVisual, RenderableT)

    Renderable interior;
    Generic<CubeMap> envMap;
    Generic<CubeMap> envMapLF;
    Generic<CubeMap> irMap;

    Texture2D rCurve;
    Texture2D gCurve;
    Texture2D bCurve;
    Texture2D rCurve2;
    Texture2D gCurve2;
    Texture2D bCurve2;
    V2 rDir;
    V2 gDir;
    V2 bDir;

    SystemVisual(System* self) {
      RNG rng = RNG_MTG(self->Seeded.seed);

      /* CubeMap. */ {
        Generator_Nebula_Args args;
        args.seed = rng->GetFloat(0, 1000);
        args.roughness = rng->GetFloat();
        args.offset = rng->GetV4();

        MessageGetColor starColor;
        self->star->Send(starColor);
        args.color1 = starColor.color;
        args.color2 = Mix(args.color1, GenerateStarColor(rng), 0.5f);
        args.starDir = Normalize(self->star->GetPos());

        envMap = DiskCached(
          Generator_Nebula(args),
          Stringize() | "nebula_" | self->Seeded.seed);
      }

      /* Interior. */ {
        interior = Renderable_Starfield(
          self->Seeded.seed,
          Abs(kBaseStarCount + 2000 * rng->GetGaussian()));
      }

      envMapLF = DiskCached(
        Generator_Blur(envMap, 0.1f, 1024, 512),
        Stringize() | "sysbg_" | self->Seeded.seed | "_blurred");

      irMap = Generator_IRMap(envMap, 1024);

      RandomizeColors(RNG_MTG(rng->GetInt() + 3));
    }

    /* Drawn through the System's interior, not as a renderable of its own. */
    void Render(DrawState*) const {}

    void RandomizeColors(RNG const& rng) {
      rCurve = CreateColorCurve(rng);
      gCurve = CreateColorCurve(rng);
      bCurve = CreateColorCurve(rng);
      rCurve2 = CreateColorCurve(rng);
      gCurve2 = CreateColorCurve(rng);
      bCurve2 = CreateColorCurve(rng);
      rDir = CreateColorDir(rng);
      gDir = CreateColorDir(rng);
      bDir = CreateColorDir(rng);
    }

    V2 CreateColorDir(RNG const& rng) {
      return rng->GetFloat() < 0.5f ? V2(1, 0) : V2(0, 1);
    }

    Texture2D CreateColorCurve(RNG const& rng) {
      Texture2D curve = Texture_Create(kColorPoints, 1, TextureFormat::R8);
      curve->SetMagFilter(TextureFilter::Linear);
      curve->SetMinFilter(TextureFilterMip::Linear);
      curve->SetMaxLod(0);
      curve->SetMinLod(0);

      Vector<float> controlPoints;
      controlPoints.push(0);
      controlPoints.push(1);

      float variation = kColorVariation;
      for (int i = 0; i < kColorIterations; ++i) {
        Vector<float> newControlPoints;
        for (size_t j = 0; j + 1 < controlPoints.size(); ++j) {
          float v1 = controlPoints[j + 0];
          float v2 = controlPoints[j + 1];
          float v = (v1 + v2) / 2.0f;
          v += variation * rng->GetGaussian();
          newControlPoints.push(v1);
          newControlPoints.push(v);
        }

        newControlPoints.push(controlPoints.back());
        controlPoints = newControlPoints;
        variation *= kColorLacunarity;
      }

      Vector<float> points;
      for (size_t i = 0; i < kColorPoints; ++i) {
        float t = (float)i / (float)(kColorPoints - 1);
        Vector<float> interpolated = controlPoints;

        while (interpolated.size() > 1) {
          Vector<float> newInterpolated;
          for (size_t j = 0; j + 1 < interpolated.size(); ++j) {
            float v1 = interpolated[j + 0];
            float v2 = interpolated[j + 1];
            newInterpolated.push(Mix(v1, v2, t));
          }
          interpolated = newInterpolated;
        }

        points.push(interpolated[0]);
      }

      curve->SetData(
        0, 0, kColorPoints, 1,
        PixelFormat::Red, DataFormat::Float, points.data());
      return curve;
    }
  };

  SystemVisual* VisualOf(ObjectT* object) {
    return (SystemVisual*)((System*)object)->visual.t;
  }

  void BeginDrawInterior(ObjectT* object, DrawState* state) {
    System* self = (System*)object;
    SystemVisual* v = VisualOf(object);
    if (Keyboard_Pressed(Key_F6))
      v->RandomizeColors(RNG_MTG(rand()));

    state->envMap.push(v->envMap());
    state->envMapLF.push(v->envMapLF());

    MessageGetColor starColor;
    self->star->Send(starColor);
    DrawState_Push("starColor", (V3)starColor.color);
    DrawState_Push("starPos", (V3)self->star->GetPos() - state->view->transform.pos);

    CubeMap const& irMap = v->irMap();
    DrawState_Push("irMap", irMap);

    DrawState_Push("rCurve", v->rCurve);
    DrawState_Push("gCurve", v->gCurve);
    DrawState_Push("bCurve", v->bCurve);
    DrawState_Push("rCurve2", v->rCurve2);
    DrawState_Push("gCurve2", v->gCurve2);
    DrawState_Push("bCurve2", v->bCurve2);
    DrawState_Push("rDir", v->rDir);
    DrawState_Push("gDir", v->gDir);
    DrawState_Push("bDir", v->bDir);
    DrawState_Push("colorPoints", (float)kColorPoints);
  }

  void OnDrawInterior(ObjectT* object, DrawState* state) {
    VisualOf(object)->interior->Render(state);
  }

  void EndDrawInterior(ObjectT*, DrawState* state) {
    DrawState_Pop("starColor");
    DrawState_Pop("starPos");

    DrawState_Pop("rCurve");
    DrawState_Pop("gCurve");
    DrawState_Pop("bCurve");
    DrawState_Pop("rCurve2");
    DrawState_Pop("gCurve2");
    DrawState_Pop("bCurve2");
    DrawState_Pop("rDir");
    DrawState_Pop("gDir");
    DrawState_Pop("bDir");
    DrawState_Pop("colorPoints");
    DrawState_Pop("irMap");

    state->envMap.pop();
    state->envMapLF.pop();
  }

  void Create(ObjectT* object, Renderable& visual) {
    visual = new SystemVisual((System*)object);
  }

  bool registered =
    (Game::RegisterVisual("System", Create),
     Game::RegisterDraw("System", Game::DrawPhase::BeginInterior, BeginDrawInterior),
     Game::RegisterDraw("System", Game::DrawPhase::Interior, OnDrawInterior),
     Game::RegisterDraw("System", Game::DrawPhase::EndInterior, EndDrawInterior), true);
}
