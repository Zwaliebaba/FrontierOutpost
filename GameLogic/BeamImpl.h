#ifndef BeamImpl_h__
#define BeamImpl_h__

#include "Beam.h"

#include "Collidable.h"
#include "Interior.h"
#include "Motion.h"
#include "Queryable.h"

#include "WeaponType.h"

#include "AutoPtr.h"
#include "LteMath.h"
#include "Matrix.h"
#include "Ray.h"
#include "Renderable.h"

#include "SoundEngine.h"
#include "Visual.h"

const size_t kUpdatePeriod = 2;

AutoClassDerived(BeamImpl, Beam,
  Object, hitObject,
  float, length,
  float, age,
  float, targetLength,
  int, tick,
  bool, soundPlayed)

  DERIVED_TYPE_EX(BeamImpl)

  BeamImpl() :
    length(0),
    age(0),
    targetLength(0),
    tick(RandI(0, kUpdatePeriod)),
    soundPlayed(false)
  {
    /* Drawn by the client's visual, where there is a client (ADR-016). */
    Drawable.renderable = (Renderable)Game::CreateVisual("Beam", this);
  }

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);

    if (!Damager.source) {
      Delete();
      return;
    }

    age += state.dt;
    length = Mix(length, targetLength, 5 * state.dt);

    if (--tick <= 0) {
      WorldRay ray(origin, Normalize(direction + RandV3(-0.01f, 0.01f)));
      targetLength = 0;
      float t;
      hitObject = GetContainer()->QueryInterior(
        ray, t, Damager.type->range, nullptr, true, RaycastCanCollideBidirectional, this);

      targetLength = hitObject ? t : Damager.type->range;
      tick = (int)(Rand(0.9f, 1.1f) * kUpdatePeriod);
    }

    if (hitObject)
      Damager.Hit(
        this, hitObject, origin + direction * targetLength,
        state.dt);

    if (!soundPlayed) {
      soundPlayed = true;
      Sound_Play3D("weapon/beam1_loop.wav", this, 0, 1.f, 1, true);
    }
  }

  bool CanCollide(const ObjectT* o) const {
    if (Damager.source && o->GetRoot() == Damager.source->GetRoot())
      return false;
    return true;
  }
};

#endif
