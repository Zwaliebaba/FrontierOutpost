#include "../RenderPasses.h"

#include "Game/Light.h"
#include "Game/Object.h"

#include "LTE/Array.h"
#include "LTE/DrawState.h"
#include "LTE/Location.h"
#include "LTE/Meshes.h"
#include "LTE/Renderer.h"
#include "LTE/RendererCore.h"
#include "LTE/Shader.h"
#include "LTE/Texture2D.h"
#include "LTE/Vector.h"
#include "LTE/View.h"

#include "Module/FrameTimer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

const float kCullBrightness = 0.01f;
const float kOcclusionSpeed = 8.0f;
const uint kMaxFlares = 64;

namespace {
  AutoClass(LensFlare,
    V2, center,
    V2, scale,
    Color, color,
    float, depth,
    Light*, light)

    LensFlare() {}

    friend bool operator<(LensFlare const& a, LensFlare const& b) {
      return
        Luminance(a.color) * a.scale.GetMax() >
        Luminance(b.color) * b.scale.GetMax();
    }
  };

  struct LensFlares : public RenderPassT {
    Shader shader;
    Shader shaderComposite;
    Shader shaderComputeVisibility;
    Texture2D flareTexture;
    Texture2D dirtTexture;
    Texture2D queryBuffer;
    Texture2D resultBuffer;
    Array<V4> queryBufferData;
    Array<float> queryResultData;
    Vector<LensFlare> flares;
    /* The occlusion is read back while the next frames are drawn, not waited
       for (plan section 5.3): the ticket of the read in flight, 0 for none,
       the lights it measured, held until its result is in, and the time since
       it was asked for. */
    std::uint64_t read;
    Vector<LightRef> readLights;
    float readTime;
    DERIVED_TYPE_EX(LensFlares)

    LensFlares() :
      shader(Shader_Create("identity.jsl", "lensflare.jsl")),
      shaderComposite(Shader_Create("identity.jsl", "post/lensflare_composite.jsl")),
      shaderComputeVisibility(Shader_Create("identity.jsl", "compute/lensflare_visibility.jsl")),
      dirtTexture(Texture_LoadFrom(Location_Texture("lensdirt.jpg"))),
      queryBufferData(kMaxFlares),
      queryResultData(kMaxFlares),
      read(0),
      readTime(0)
    {
      queryBuffer = Texture_Create(kMaxFlares, 1, TextureFormat::RGBA32F);
      resultBuffer = Texture_Create(kMaxFlares, 1, TextureFormat::R32F);
    }

    char const* GetName() const {
      return "Lens Flares";
    }

    void OnRender(DrawState* state) {
      RendererZBuffer zBuffer(false);

      /* Generate flare texture. */ {
        static Shader generate = Shader_Create("identity.jsl", "gen/lensflare.jsl");
        if (!flareTexture) {
          flareTexture = Texture_Create(1024, 1024, TextureFormat::R16F);
          Texture_Generate(flareTexture, generate);
        }
      }

      Texture2D const& targetBuffer = state->tertiary;

      /* Draw. */ {
        targetBuffer->Bind(0);
        DrawState_Link(shader);

        RendererBlendMode blendMode(BlendMode::Additive);
        Renderer_Clear();
        Renderer_SetShader(*shader);

        flares.clear();

        for (size_t i = 0; i < state->lights.size(); ++i) {
          Light* light = (Light*)state->lights[i];
          if (!light->parent || !light->flare)
            continue;
          if (light->color < kCullBrightness)
            continue;

          Transform const& transform = light->GetTransform();
          float r = 20.0f * light->radius * transform.scale.GetGeometricAverage();

          V3 projMin = state->view->Project(
            transform.pos - r * (state->view->transform.up + state->view->transform.right));
          V3 projMax = state->view->Project(
            transform.pos + r * (state->view->transform.up + state->view->transform.right));
          if (projMin.z < 0 || projMax.z < 0)
            continue;

          Position projected =
            state->view->proj.TransformPoint(
            state->view->view.TransformPoint(transform.pos));

          LensFlare flare(
            0.5f * (projMin + projMax).GetXY(),
            0.5f * (projMax - projMin).GetXY(),
            light->color,
            Min((float)projected.z, state->view->zFar),
            light);

          flares.push(flare);
        }

        std::sort(flares.begin(), flares.end());

        for (size_t i = 0; i < flares.size() && i < kMaxFlares; ++i) {
          LensFlare const& flare = flares[i];

          (*shader)
            ("baseColor", flare.color)
            ("center", flare.center)
            ("depth", flare.depth)
            ("opacity", flare.light->visibility)
            ("texture", flareTexture);

          Renderer_DrawQuad(
            flare.center - flare.scale,
            flare.center + flare.scale, -1, 1);
        }

        targetBuffer->Unbind();
      }

      /* Composite. */ {
        state->secondary->Bind(0);
        DrawState_Link(shaderComposite);
        (*shaderComposite)
          ("texture1", state->primary)
          ("texture2", targetBuffer)
          ("dirtTexture", dirtTexture);

        Renderer_SetShader(*shaderComposite);
        Renderer_DrawQuad();

        state->secondary->Unbind();
        state->Flip();
      }

      /* Compute Occlusion. */ {
        Neuron::DrawContext& context = Renderer_Context();
        readTime += FrameTimer_Get();

        /* The read in flight, once the GPU has copied it. Each light it
           measured moves toward its result as far as it would have moved,
           frame by frame, in the time since. Until then no other read starts. */
        if (read) {
          std::vector<std::byte> texels;
          if (!context.TakeRead(read, texels))
            return;
          size_t const results = Min(readLights.size(), texels.size() / sizeof(float));
          std::memcpy(queryResultData.data(), texels.data(), results * sizeof(float));
          float factor = 1.0f - Exp(-kOcclusionSpeed * readTime);
          for (size_t i = 0; i < results; ++i)
            readLights[i]->visibility =
              Mix(readLights[i]->visibility, queryResultData[i], factor);
          read = 0;
          readLights.clear();
        }

        uint const count = Min(kMaxFlares, (uint)flares.size());
        if (!count)
          return;
        for (uint i = 0; i < count; ++i)
          queryBufferData[i] =
            V4(flares[i].center.x, flares[i].center.y, flares[i].depth, 0);

        queryBuffer->SetData(
          0, 0,
          count, 1,
          PixelFormat::RGBA,
          DataFormat::Float,
          queryBufferData.data());

        resultBuffer->Bind(0);

        (*shaderComputeVisibility)
          ("flareBuffer", queryBuffer);

        DrawState_Link(shaderComputeVisibility);
        Renderer_SetShader(*shaderComputeVisibility);
        Renderer_DrawQuad();

        resultBuffer->Unbind();
        read = context.RequestRead(Texture2D_GetGpu(*resultBuffer)->texture, 0, 0);
        readTime = 0;
        for (uint i = 0; i < count; ++i)
          readLights.push(flares[i].light);
      }
    }
  };
}

DefineFunction(RenderPass_LensFlares) {
  return new LensFlares;
}
