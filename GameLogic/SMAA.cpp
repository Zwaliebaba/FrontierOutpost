#include "GameRenderPasses.h"

#include "Array.h"
#include "AutoPtr.h"
#include "DrawState.h"
#include "Location.h"
#include "Renderer.h"
#include "Shader.h"
#include "Texture2D.h"

#include "SMAA_AreaTex.h"
#include "SMAA_SearchTex.h"

namespace {
  struct SMAA : public RenderPassT {
    Texture2D areaTex;
    Texture2D searchTex;
    Shader edgePass;
    Shader weightPass;
    Shader blendPass;
    DERIVED_TYPE_EX(SMAA)

    SMAA() :
      edgePass(Shader_Create("smaa_1.jsl", "smaa_1.jsl")),
      weightPass(Shader_Create("smaa_2.jsl", "smaa_2.jsl")),
      blendPass(Shader_Create("smaa_3.jsl", "smaa_3.jsl"))
    {
      AutoPtr<Array<uchar> > areaTexData =
        Location_Texture("SMAA_AreaTex.bin")->Read();
      AutoPtr<Array<uchar> > searchTexData =
        Location_Texture("SMAA_SearchTex.bin")->Read();

      areaTex = Texture_Create(
        AREATEX_WIDTH,
        AREATEX_HEIGHT,
        TextureFormat::RG8,
        areaTexData->data());

      areaTex->SetMagFilter(TextureFilter::Linear);
      areaTex->SetMinFilter(TextureFilterMip::Linear);
      areaTex->SetWrapMode(TextureWrapMode::ClampToEdge);

      searchTex = Texture_Create(
        SEARCHTEX_WIDTH,
        SEARCHTEX_HEIGHT,
        TextureFormat::R8,
        searchTexData->data());

      searchTex->SetMagFilter(TextureFilter::Nearest);
      searchTex->SetMinFilter(TextureFilterMip::Nearest);
      searchTex->SetWrapMode(TextureWrapMode::ClampToEdge);
    }

    char const* GetName() const {
      return "SMAA";
    }

    void OnRender(DrawState* state) {
      DrawState_Link(edgePass);
      (*edgePass)
        ("texture", state->primary);

      state->tertiary->Bind(0);
      Renderer_Clear();
      Renderer_SetShader(*edgePass);
      Renderer_DrawFSQ();
      state->tertiary->Unbind();

      DrawState_Link(weightPass);
      (*weightPass)
        ("edgeBuffer", state->tertiary)
        ("areaBuffer", areaTex)
        ("searchBuffer", searchTex);

      state->secondary->Bind(0);
      Renderer_Clear();
      Renderer_SetShader(*weightPass);
      Renderer_DrawFSQ();
      state->secondary->Unbind();

#if 1
      DrawState_Link(blendPass);
      (*blendPass)
        ("texture", state->primary)
        ("blendWeights", state->secondary);

      state->tertiary->Bind(0);
      Renderer_SetShader(*blendPass);
      Renderer_DrawFSQ();
      state->tertiary->Unbind();
      Swap(state->primary, state->tertiary);
#else
      Swap(state->primary, state->secondary);
#endif

    }
  };
}

DefineFunction(RenderPass_SMAA) {
  return new SMAA;
}
