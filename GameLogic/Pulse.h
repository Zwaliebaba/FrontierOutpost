#ifndef Pulse_h__
#define Pulse_h__

#include "Objects.h"
#include "Damager.h"
#include "Drawable.h"
#include "Interior.h"
#include "Orientation.h"
#include "Queryable.h"
#include "Light.h"
#include "WeaponType.h"
#include "Loader.h"
#include "Matrix.h"
#include "Meshes.h"
#include "Pool.h"
#include "Ray.h"
#include "Renderable.h"
#include "View.h"
#include "Debug.h"
#include "Visual.h"

const float kSizeMult = 32;
const float kLengthMult = 4;


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

  Pulse() :
    age(0),
    opacity(1)
  {
    /* Drawn by the client's visual, where there is a client (ADR-016). */
    Drawable.renderable = (Renderable)Game::CreateVisual("Pulse", this);
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

#endif
