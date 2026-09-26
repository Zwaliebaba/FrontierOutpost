#ifndef Explosion_h__
#define Explosion_h__

#include "Objects.h"
#include "Attachable.h"
#include "Drawable.h"
#include "Orientation.h"
#include "Light.h"
#include "Matrix.h"
#include "LteMath.h"
#include "Meshes.h"
#include "Pool.h"
#include "Renderable.h"
#include "View.h"
#include "Visual.h"

typedef ObjectWrapper
  < Component_Attachable
  < Component_Drawable
  < Component_Orientation
  < ObjectWrapperTail<ObjectType_Explosion>
  > > > >
  ExplosionBaseT;

AutoClassDerived(Explosion, ExplosionBaseT,
  LightRef, light,
  ExplosionType, type,
  float, age,
  float, duration)

  DERIVED_TYPE_EX(Explosion)
  POOLED_TYPE

  Explosion() :
    age(0),
    duration(1)
  {
    /* Drawn by the client's visual, where there is a client (ADR-016). */
    Drawable.renderable = (Renderable)Game::CreateVisual("Explosion", this);
  }

  float GetOpacity() const {
    return Sqrt(Saturate(1.f - age / duration));
  }

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);

    age += state.dt;
    if (age > duration) {
      Delete();
      return;
    }

    if (!light)
      light = Light_Create(this);
    light->color = GetOpacity() * Color(2.8f, 1.0f, 0.5f);
  }
};

#endif
