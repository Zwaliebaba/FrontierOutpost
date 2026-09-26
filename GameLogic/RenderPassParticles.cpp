#include "GameRenderPasses.h"

#include "Interior.h"

#include "Object.h"
#include "RenderStyles.h"

#include "DrawState.h"
#include "ParticleSystem.h"
#include "RenderStyle.h"

namespace {
  struct Particles : public RenderPassT {
    RenderStyle style;
    DERIVED_TYPE_EX(Particles)

    Particles() :
      style(RenderStyle_Default(true))
      {}

    char const* GetName() const {
      return "Particles";
    }

    void OnRender(DrawState* state) {
      RenderStyle_Push(style);
      ObjectT* container = (ObjectT*)state->visible[0];
      container->GetInterior()->particles->Draw(state);
      RenderStyle_Pop();
    }
  };
}

DefineFunction(RenderPass_Particles) {
  return new Particles;
}
