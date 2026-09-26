#include "GameRenderPasses.h"

#include "Object.h"
#include "RenderStyles.h"

#include "DrawState.h"
#include "RenderStyle.h"
#include "StackFrame.h"
#include "Texture2D.h"

namespace {
  struct BlendedPass : public RenderPassT {
    RenderStyle style;
    DERIVED_TYPE_EX(BlendedPass)

    BlendedPass() :
      style(RenderStyle_Default(true))
      {}

    char const* GetName() const {
      return "Blended";
    }

    void OnRender(DrawState* state) {
      RenderStyle_Push(style);
      state->primary->Bind(0);
      for (size_t i = 0; i < state->visible.size(); ++i)
        ((ObjectT*)state->visible[i])->OnDraw(state);
      state->primary->Unbind();
      RenderStyle_Pop();
    }
  };
}

DefineFunction(RenderPass_Blended) {
  return new BlendedPass;
}
