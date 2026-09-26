#include "Thruster.h"

#include "GraphicsEffects.h"
#include "DrawState.h"
#include "Model.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"

/* How Thruster objects are drawn: the client's half of Thruster.cpp (ADR-016). */

namespace {
  Model gModel;
  Shader gShader;
  ShaderInstance gShaderInstance;

  void OnLoad() {
    gShader = Shader_Create("billboard_axis.jsl", "thruster_trail.jsl");
    gShaderInstance = ShaderInstance_Create(gShader);
    (*gShaderInstance)
      (RenderStateSwitch_BlendModeAdditive)
      (RenderStateSwitch_ZWritableOff);
    DrawState_Link(gShaderInstance);
    gModel = Model_Create()->Add(Mesh_Billboard(-1, 1, 0, 1), gShaderInstance);
  } bool r = RegisterLoader(OnLoad);
}

namespace {
  struct RenderComponent : public RenderableT {
    DERIVED_TYPE(RenderComponent, RenderableT)
    Pointer<Thruster> self;

    RenderComponent(Thruster* self) :
      self(self)
      {}

    Bound3 GetBound() const {
      Bound3 bound = self->Supertyped.type->GetRenderable()->GetBound();
      bound.AddPoint(V3(-1, -1, kTrailLengthMult));
      bound.AddPoint(V3( 1, -1, kTrailLengthMult));
      bound.AddPoint(V3(-1,  1, kTrailLengthMult));
      bound.AddPoint(V3( 1,  1, kTrailLengthMult));
      return bound;
    }

    void Render(DrawState* state) const {
      self->Supertyped.type->GetRenderable()->Render(state);

      float opacity = self->activation;
      if (opacity <= 0.01f)
        return;

      Transform transform = self->GetTransform() *
        Transform_Translation(self->Supertyped.type->GetOffset());

      (*gShader)
        ("axis", transform.look)
        ("size", Length(transform.scale) * V2(0.5f, opacity * kTrailLengthMult))
        ("t", self->age)
        ("baseColor", self->color);

      RenderStyle_Get()->SetTransform(transform);
      gModel->Render(state);
    }
  };

  RenderableT* Create(ObjectT* object) {
    return new RenderComponent((Thruster*)object);
  }

  bool registered = (Game::RegisterVisual("Thruster", Create), true);
}
