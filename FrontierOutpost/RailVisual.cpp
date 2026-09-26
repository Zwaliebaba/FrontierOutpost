#include "Rail.h"

#include "GraphicsEffects.h"
#include "DrawState.h"
#include "Model.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"

/* How Rail objects are drawn: the client's half of Rail.cpp (ADR-016). */

namespace {
  Model gRailModel;
  Shader gRailShader;
  ShaderInstance gRailSS;

  void OnLoad() {
    gRailShader = Shader_Create("billboard_axis.jsl", "rail.jsl");
    gRailSS = ShaderInstance_Create(gRailShader);
    (*gRailSS)
      (RenderStateSwitch_BlendModeAdditive)
      (RenderStateSwitch_ZWritableOff);
    DrawState_Link(gRailSS);
    gRailModel = Model_Create()->Add(Mesh_Billboard(-1, 1, 0, 1), gRailSS);
  } bool l = RegisterLoader(OnLoad);
}

namespace {
  struct RenderComponent : public RenderableT {
    DERIVED_TYPE(RenderComponent, RenderableT)
    Rail* self;

    RenderComponent(Rail* self) :
      self(self)
      {}
  
    Bound3 GetBound() const {
      Bound3 myBox(self->position);
      myBox.AddPoint(self->position + self->direction * self->Damager.type->range);
      return myBox;
    }

    void Render(DrawState* state) const {
      /* Frustum culling. */
      if (!state->view->CanSee(GetBound()))
        return;

      Distance distance = Length(OrthoProjection(
        (V3)(state->view->transform.pos - self->position),
        self->direction));
      const Distance cullDistance = 4000;
      if (distance > cullDistance)
        return;

      (*gRailShader)
        ("axis", self->direction)
        ("size", V2(32.0f, self->length))
        ("baseColor", self->Damager.type->color)
        ("opacity", Exp(-2.0f * self->age / kRailAge));

      RenderStyle_Get()->SetTransform(Transform_Translation(self->position));
      gRailModel->Render(state);
    }
  };

  void Create(ObjectT* object, Renderable& visual) {
    visual = new RenderComponent((Rail*)object);
  }

  bool registered = (Game::RegisterVisual("Rail", Create), true);
}
