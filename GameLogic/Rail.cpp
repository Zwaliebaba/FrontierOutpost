#include "Objects.h"

#include "Damager.h"
#include "Drawable.h"
#include "Interior.h"
#include "Queryable.h"

#include "GraphicsEffects.h"
#include "WeaponType.h"

#include "DrawState.h"
#include "Loader.h"
#include "LteMath.h"
#include "Matrix.h"
#include "Meshes.h"
#include "Model.h"
#include "Pool.h"
#include "Ray.h"
#include "Renderable.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"
#include "View.h"

const float kRailAge = 0.25f;
const float kRailSpeed = 10000;

/* TODO : Unify rail and pulse. */

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

typedef ObjectWrapper
  < Component_Damager
  < Component_Drawable
  < ObjectWrapperTail<ObjectType_Rail>
  > > >
  RailBaseT;

AutoClassDerived(Rail, RailBaseT,
  Position, position,
  V3, direction,
  V3, velocity,
  float, age,
  float, length,
  bool, cast)

  DERIVED_TYPE_EX(Rail)
  POOLED_TYPE

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

  Rail() :
    age(0),
    length(0),
    cast(false)
  {
    Drawable.renderable = (Renderable)(new RenderComponent(this));
  }

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);

    age += Rand(0.9f, 1.0f / 0.9f) * state.dt;

    if (age > kRailAge) {
      Delete();
      return;
    }

    if (!cast) {
      cast = true;
      Ray r(position, direction);
      float t;
      ObjectT* hitObject = GetContainer()
        ->QueryInterior(r, t, Damager.type->range, nullptr, true,
                        RaycastCanCollideBidirectional, this);

      if (hitObject) {
        length = t;
        if (Damager.Hit(this, hitObject, r(t), state.dt))
          Effect_BeamHit(r(t), 0, 1, Damager.type->color);
      } else
        length = Damager.type->range;
    }

    position += velocity * state.dt;
  }

  bool CanCollide(ObjectT const* o) const {
    if (Damager.source && o->GetRoot() == Damager.source->GetRoot())
      return false;
    return true;
  }
};

DERIVED_IMPLEMENT(Rail)

Object Object_Rail(
  Position const& origin,
  V3 const& direction,
  V3 const& velocity)
{
  Reference<Rail> self = new Rail;
  self->position = origin;
  self->direction = direction;
  self->velocity = velocity;
  return self;
}
