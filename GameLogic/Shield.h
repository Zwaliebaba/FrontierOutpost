#ifndef Shield_h__
#define Shield_h__

#include "Objects.h"
#include "Attachable.h"
#include "BoundingBox.h"
#include "Collidable.h"
#include "Cullable.h"
#include "Damager.h"
#include "Drawable.h"
#include "Explodable.h"
#include "Integrity.h"
#include "Orientation.h"
#include "Supertyped.h"
#include "SoundEngine.h"
#include "Geom.h"
#include "Loader.h"
#include "LteMath.h"
#include "Matrix.h"
#include "Mesh.h"
#include "Pool.h"
#include "Tuple.h"
#include "Vector.h"
#include "VectorMap.h"
#include "Visual.h"

const size_t kMaxHits = 16;
const float kMaxAge = 1;
const float kChargeTime = 60;
const float kRestoreFraction = .25f;

/* The smoothed hull of each parent's collision mesh, by the parent's renderable. Defined once, in
   Shield.cpp, now that the class is in Shield.h (ADR-016). */
VectorMap<size_t, Mesh>& Shield_MeshCache();


typedef ObjectWrapper
  < Component_Attachable
  < Component_BoundingBox
  < Component_Collidable
  < Component_Cullable
  < Component_Drawable
  < Component_Explodable
  < Component_Integrity
  < Component_Orientation
  < Component_Supertyped
  < ObjectWrapperTail<ObjectType_Shield>
  > > > > > > > > > >
  ShieldBaseT;

AutoClassDerived(Shield, ShieldBaseT,
  float, time,
  float, recharge)

  Mesh mesh;
  std::vector<V3> hitPosition;
  std::vector<float> hitAge;

  DERIVED_TYPE_EX(Shield)
  POOLED_TYPE

  Shield() :
    time(Rand(0, 10000)),
    recharge(1)
  {
    /* Drawn by the client's visual, where there is a client (ADR-016). */
    Drawable.renderable = (Renderable)Game::CreateVisual("Shield", this);
    Explodable.explosionType = ExplosionType_Plasma;
    hitAge.resize(kMaxHits, 1e10f);
    hitPosition.resize(kMaxHits, 0);
  }

  float GetCooldown() const {
    return recharge;
  }

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);

    /* Keep the shield's relative orientation fixed. */
    /* TODO : Why? Shouldn't it stay fixed automatically?? */
    Attachable.transform = Transform_Identity();
    Attachable.moved = true;

    if (!mesh) {
      size_t id = parent->GetDrawable()->renderable()->GetHash();
      if (!Shield_MeshCache()[id]) {
        Mesh source = parent->GetDrawable()->renderable()->GetCollisionMesh();
        /* TODO : Thread this, since SmoothHull is expensive. */
        if (source && source->GetTriangles())
          Shield_MeshCache()[id] = Geom_SmoothHull(source, 5, 5);
      }
      mesh = Shield_MeshCache()[id];
    }

    time += state.dt;
    for (size_t i = 0; i < hitAge.size(); ++i)
      hitAge[i] += state.dt;

    float chargeUnit = parent->GetPowerFraction() * (state.dt / kChargeTime);
    if (!IsAlive()) {
      if ((recharge -= chargeUnit) <= 0) {
        Explodable.exploded = false;
        Integrity.health = kRestoreFraction * Integrity.maxHealth;
        recharge = 1;
      }
    } else {
      Integrity.health = Min(
        Integrity.maxHealth,
        (Health)(Integrity.health + chargeUnit * Integrity.maxHealth));
    }

    SetScale(parent->GetScale());
  }

  bool CanCollide(ObjectT const* other) const {
    ComponentDamager const* d = other->GetDamager();
    if (!d)
      return false;
    return IsAlive() &&
           d->source &&
           d->source->GetRoot() != this->GetRoot();
  }

  void OnCollide(
    ObjectT* self,
    ObjectT* other,
    Position const& pSelf,
    Position const& pOther)
  {
    if (hitAge.size() >= kMaxHits) {
      hitAge.erase(hitAge.begin());
      hitPosition.erase(hitPosition.begin());
    }

    hitAge.push_back(0);
    hitPosition.push_back(pSelf);
    Sound_Play3D("shield/hit.wav", self,
      self->GetTransform().InversePoint(pSelf), 0.25f);
  }
};


#endif
