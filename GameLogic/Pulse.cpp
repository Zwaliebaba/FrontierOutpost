#include "Objects.h"

#include "Damager.h"
#include "Drawable.h"
#include "Interior.h"
#include "Orientation.h"
#include "Queryable.h"

#include "Light.h"
#include "WeaponType.h"

#include "DrawState.h"
#include "Loader.h"
#include "Matrix.h"
#include "Meshes.h"
#include "Model.h"
#include "Pool.h"
#include "Ray.h"
#include "Renderable.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"
#include "View.h"

#include "Debug.h"

const float kSizeMult = 32;
const float kLengthMult = 4;

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

typedef ObjectWrapper
  < Component_Damager
  < Component_Drawable
  < Component_Orientation
  < ObjectWrapperTail<ObjectType_Pulse>
  > > > >
  PulseBaseT;

AutoClassDerived(Pulse, PulseBaseT,
  LightRef, light,
  Position, lastPos,
  V3, drift,
  float, speed,
  float, width,
  float, length,
  float, age,
  float, opacity)

  DERIVED_TYPE_EX(Pulse)
  POOLED_TYPE

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

  Pulse() :
    age(0),
    opacity(1)
  {
    Drawable.renderable = (Renderable)(new RenderComponent(this));
  }

  void OnCreate() {
    BaseType::OnCreate();
    lastPos = GetPos();
  }

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);

    /* TODO : Need a cleaner way to specify functionality that needs to happen
     *        "once, before update, but after all initialization. */
    if (!light)  {
      light = Light_Create(this);
      light->radius = 1.0f;
      light->flare = false;
    }
    light->color = Damager.type->color * opacity;

    age += state.dt;
    V3 dP = (speed * Orientation.transform.look + drift) * state.dt;
    Orientation.GetTransformW().pos += dP;

    /* CRITICAL. */
    Position pos = GetPos();
    WorldRay r = WorldRay::FromPoints(lastPos, pos);
    lastPos = pos;

    float t;
    ObjectT* hitObject = GetContainer()
      ->QueryInterior(r, t, 1, nullptr, true, RaycastCanCollideBidirectional, this);

    if (hitObject) {
      if (Damager.Hit(this, hitObject, r(t), state.dt)) {
        Delete();
        return;
      }
    }

    float maxAge = Damager.type->GetRange() / Damager.type->GetSpeed();

    /* Smoothly fade the pulse out as it gets close to maximal range, then
       delete it when it gets there. */
    if (age > maxAge) {
      Delete();
      return;
    } else {
      opacity = Sqrt(1.0f - age / maxAge);
    }
  }

  bool CanCollide(const ObjectT* o) const {
    if (Damager.source && o->GetRoot() == Damager.source->GetRoot())
      return false;
    return true;
  }
};

DERIVED_IMPLEMENT(Pulse)

Object Object_Pulse(
  V3 const& velocity,
  V3 const& drift,
  float width)
{
  Reference<Pulse> self = new Pulse;
  self->SetLook(Normalize(velocity));
  self->speed = Length(velocity);
  self->drift = drift;
  self->width = kSizeMult * width;
  self->length = kLengthMult * self->width;
  return self;
}
