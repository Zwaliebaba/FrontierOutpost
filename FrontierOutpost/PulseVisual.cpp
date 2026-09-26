#include "Pulse.h"

#include "DrawState.h"
#include "Model.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"

/* How Pulse objects are drawn: the client's half of Pulse.cpp (ADR-016). */

namespace {
  Model gHeadModel;
  Model gTailModel;
  Shader gHeadShader;
  Shader gTailShader;
  ShaderInstance gHeadSS;
  ShaderInstance gTailSS;

  void OnLoad() {
    gHeadShader = Shader_Create("billboard.jsl", "pulse_head.jsl");
    gTailShader = Shader_Create("billboard_axis.jsl", "pulse_tail.jsl");
    gHeadSS = ShaderInstance_Create(gHeadShader);
    gTailSS = ShaderInstance_Create(gTailShader);
    (*gHeadSS)
      (RenderStateSwitch_BlendModeAdditive)
      (RenderStateSwitch_ZWritableOff);
    (*gTailSS)
      (RenderStateSwitch_BlendModeAdditive)
      (RenderStateSwitch_ZWritableOff);
    DrawState_Link(gHeadSS);
    DrawState_Link(gTailSS);
    gHeadModel = Model_Create()->Add(Mesh_Billboard(-1, 1, -1, 1), gHeadSS);
    gTailModel = Model_Create()->Add(Mesh_Billboard(-1, 1,  0, 1), gTailSS);
  } bool r = RegisterLoader(OnLoad);
}

namespace {
  struct RenderComponent : public RenderableT {
    DERIVED_TYPE(RenderComponent, RenderableT)
    Pulse* self;

    RenderComponent(Pulse* self) :
      self(self)
      {}

    Bound3 GetBound() const {
      Position pos = self->GetPos();
      Bound3 myBox(pos);
      myBox.AddPoint(pos - self->GetLook() * (self->length * self->speed));
      return myBox;
    }

    void Render(DrawState* state) const {
      /* Frustum culling. */
      if (self->age == 0 || !state->view->CanSee(GetBound()))
        return;

      Transform const& transform = self->GetTransform();

      /* Distance culling. */
      float cullDistance = 250.0f;
      if (Length(state->view->transform.pos - transform.pos) > cullDistance * self->width)
        return;

      float w = self->width;
      w += 0.5f * self->Damager.type->properties.y;
      RenderStyle_Get()->SetTransform(transform);

      (*gTailShader)
        ("axis", -transform.look)
        ("size", V2(w, self->length))
        ("color", self->Damager.type->color)
        ("opacity", self->opacity);
      gTailModel->Render(state);

      (*gHeadShader)
        ("billboardSize", w)
        ("color", self->Damager.type->color)
        ("opacity", self->opacity);
      gHeadModel->Render(state);
    }
  };

  void Create(ObjectT* object, Renderable& visual) {
    visual = new RenderComponent((Pulse*)object);
  }

  bool registered = (Game::RegisterVisual("Pulse", Create), true);
}
