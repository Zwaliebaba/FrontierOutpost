#include "BeamImpl.h"

#include "DrawState.h"
#include "Loader.h"
#include "Meshes.h"
#include "Model.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"

/* How a beam is drawn: the client's half of Beam.cpp (ADR-016). */

namespace {
  Mesh gMesh;
  ShaderInstance gShader;

  void OnLoad() {
    gMesh = Mesh_Billboard(-1, 1, 0, 1);
    gShader = ShaderInstance_Create("billboard_axis.jsl", "beam.jsl");
    (*gShader)
      (RenderStateSwitch_BlendModeAdditive)
      (RenderStateSwitch_ZWritableOff);
    DrawState_Link(gShader);
  } bool r = RegisterLoader(OnLoad);

  struct RenderComponent : public RenderableT {
    DERIVED_TYPE(RenderComponent, RenderableT)
    BeamImpl* self;

    RenderComponent(BeamImpl* self) :
      self(self)
      {}

    void Render(DrawState* state) const {
      if (!self->Damager.source)
        return;

      (*gShader)
        ("t", self->age)
        ("axis", Normalize(self->direction))
        ("size", V2(6.f * self->width, self->length))
        ("thinness", 10.f)
        ("baseColor", self->Damager.type->color);

      RenderStyle const& style = RenderStyle_Get();
      style->SetTransform(Transform_Translation(self->origin));
      style->SetShader(gShader);
      style->Render(gMesh);
    }
  };

  RenderableT* Create(ObjectT* object) {
    return new RenderComponent((BeamImpl*)object);
  }

  bool registered = (Game::RegisterVisual("Beam", Create), true);
}
