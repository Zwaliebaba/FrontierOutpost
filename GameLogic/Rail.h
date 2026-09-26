#ifndef Rail_h__
#define Rail_h__

#include "Objects.h"
#include "Damager.h"
#include "Drawable.h"
#include "Interior.h"
#include "Queryable.h"
#include "WeaponType.h"
#include "Loader.h"
#include "LteMath.h"
#include "Matrix.h"
#include "Meshes.h"
#include "Pool.h"
#include "Ray.h"
#include "Renderable.h"
#include "View.h"
#include "GraphicsEffects.h"
#include "Visual.h"

const float kRailAge = 0.25f;
const float kRailSpeed = 10000;

/* TODO : Unify rail and pulse. */


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

  Rail() :
    age(0),
    length(0),
    cast(false)
  {
    /* Drawn by the client's visual, where there is a client (ADR-016). */
    Renderable visual;
    Game::CreateVisual("Rail", this, visual);
    Drawable.renderable = visual;
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

#endif
