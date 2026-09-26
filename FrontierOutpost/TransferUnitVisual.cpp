#include "TransferUnit.h"

#include "Particles.h"
#include "DrawState.h"
#include "ParticleSystem.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"

/* How TransferUnit objects are drawn: the client's half of TransferUnit.cpp (ADR-016). */

namespace {
  Mesh gMesh;
  ShaderInstance gShader;
}

namespace {
  struct RenderComponent : public RenderableT {
    DERIVED_TYPE(RenderComponent, RenderableT)
    Pointer<TransferUnit> self;

    RenderComponent(TransferUnit* self) :
      self(self)
      {}

    Bound3 GetBound() const {
      if (self->active < 0.001f)
        return Bound3(0);

      float r = Length(self->targetPos.value - self->GetPos());
      return Bound3(-r, r);
    }

    void Render(DrawState* state) const {
      if (self->active < 0.001f)
        return;
    
      if (!gMesh) {
        gMesh = Mesh_Billboard(-1, 1, 0, 1);
        gShader = ShaderInstance_Create("billboard_axis.jsl", "transferbeam.jsl");
        (*gShader)
          (RenderStateSwitch_BlendModeAdditive)
          (RenderStateSwitch_ZWritableOff);
        DrawState_Link(gShader);
      }

      V3 dir = self->targetPos - self->GetTransform().pos;
      (*gShader)
        ("axis", Normalize(dir))
        ("color", self->active.value * V4(self->color.value, 1.0f))
        ("size",
          V2(kWidthMult * Length(self->GetScale()),
             Min(Length(dir), self->Supertyped.type->GetRange())))
        ("t", self->age);

      RenderStyle const& style = RenderStyle_Get();
      style->SetTransform(Transform_Translation(self->GetTransform().pos));
      style->SetShader(gShader);
      style->Render(gMesh);
    }
  };

  RenderableT* Create(ObjectT* object) {
    return new RenderComponent((TransferUnit*)object);
  }

  bool registered = (Game::RegisterVisual("TransferUnit", Create), true);
}
