#include "LteRenderPasses.h"

#include "DrawState.h"
#include "LteMath.h"
#include "Renderer.h"
#include "ShaderInstance.h"
#include "StackFrame.h"
#include "Texture2D.h"

namespace {
  void DrawSecondaryFSQ(DrawState* state) {
    state->secondary->Bind(0);
    Renderer_DrawFSQ();
    state->secondary->Unbind();
    state->Flip();
  }

  struct RenderPassPost : public RenderPassT {
    String shaderPath;
    String name;
    Shader shader;
    ShaderInstance shaderInstance;
    DERIVED_TYPE_EX(RenderPassPost)

    RenderPassPost() {}

    RenderPassPost(String const& vertPath, String const& fragPath) :
      shaderPath(fragPath),
      name("Effect <" + shaderPath + ">"),
      shader(Shader_Create(vertPath, fragPath))
    {
      shaderInstance = ShaderInstance_Create(shader);
    }

    RenderPassPost(String const& fragPath) :
      shaderPath(fragPath),
      name("Effect <" + fragPath + ">")
    {
      shader = Shader_Create("identity.jsl", fragPath);
      shaderInstance = ShaderInstance_Create(shader);
    }

    char const* GetName() const {
      return name.c_str();
    }

    ShaderInstanceT* GetShader() {
      return shaderInstance;
    }

    void OnRender(DrawState* state) {
      shaderInstance->Begin();
      DrawState_Link(shader);
      /* Every effect is offered a seed, and those that do not dither leave it
         unread. Rand() is drawn either way, so rand()'s sequence is the same. */
      float const seed = Rand();
      int const seedIndex = shader->QueryUniformLocation("seed");
      if (seedIndex >= 0)
        (*shader)(seedIndex, seed);
      (*shader)("texture", state->primary);
      Renderer_SetShader(*shader);
      DrawSecondaryFSQ(state);
      shaderInstance->End();
    }
  };
}

DefineFunction(RenderPass_PostFilter) {
  return new RenderPassPost(args.shaderPath);
}

DefineFunction(RenderPass_Tonemap) {
  return RenderPass_PostFilter("post/tonemap.jsl");
}
