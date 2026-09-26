#ifndef Thruster_h__
#define Thruster_h__

#include "Objects.h"
#include "Attachable.h"
#include "BoundingBox.h"
#include "Collidable.h"
#include "Cullable.h"
#include "Drawable.h"
#include "Explodable.h"
#include "Integrity.h"
#include "Motion.h"
#include "Orientation.h"
#include "Pluggable.h"
#include "Scriptable.h"
#include "Supertyped.h"
#include "Light.h"
#include "Messages.h"
#include "Color.h"
#include "Loader.h"
#include "LteMath.h"
#include "Matrix.h"
#include "Meshes.h"
#include "Pointer.h"
#include "Pool.h"
#include "Ray.h"
#include "Renderable.h"
#include "View.h"
#include "Visual.h"

const float kTrailLengthMult = 8;
const float kAcceleration = 1;
const V3 kBoostColor = V3(0.2f, 0.7f, 1.0f);


typedef ObjectWrapper
  < Component_Attachable
  < Component_BoundingBox
  < Component_Collidable
  < Component_Cullable
  < Component_Drawable
  < Component_Explodable
  < Component_Integrity
  < Component_Orientation
  < Component_Pluggable
  < Component_Scriptable
  < Component_Supertyped
  < ObjectWrapperTail<ObjectType_Thruster>
  > > > > > > > > > > > >
  ThrusterBaseT;

AutoClassDerived(Thruster, ThrusterBaseT,
  float, activation,
  float, age,
  Color, color,
  float, cruise,
  float, thrust,
  LightRef, light)

  DERIVED_TYPE_EX(Thruster)
  POOLED_TYPE

  Thruster() :
    activation(0),
    age(Rand(0, 100)),
    cruise(0),
    thrust(0)
    {}

  /* For performance, only check collisions with things that can damage the
   * thruster. All others are ignored. */
  bool CanCollide(ObjectT const* other) const {
    return other->GetDamager();
  }

  float GetBasePower() const {
    return Supertyped.type->GetPowerDrain();
  }

  float GetPowerFraction() const {
    return Pluggable.powerIn / GetBasePower();
  }

  float GetOutput() {
    return GetPowerFraction() * activation;
  }

  float GetOvercharge() const {
    return Saturate(2.0f * (GetPowerFraction() - 1.0f));
  }

  void OnMessage(Data& m) {
    BaseType::OnMessage(m);
    if (m.type == Type_Get<MessageCruise>())
      cruise = 1.0f;

    else if (m.type == Type_Get<MessageThrustAngular>()) {
      MessageThrustAngular const& v = m.Convert<MessageThrustAngular>();
      ThrustAngular(v.direction, v.amount);
    }

    else if (m.type == Type_Get<MessageThrustLinear>())
      Thrust(m.Convert<MessageThrustLinear>().direction);
  }

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);

    Pluggable.powerRequest = GetBasePower() * (1.0f + 100.0f * cruise);

    if (!light) {
      light = Light_Create(this);
      light->Attachable.transform =
        Transform_Translation(Supertyped.type->GetOffset());
    }

    age += state.dt;

    if (!IsAlive() || !GetRoot()->IsAlive())
      thrust = 0;

    activation = Mix(activation, thrust, 1.0f - Exp(-kAcceleration * state.dt));
    cruise *= Exp(-state.dt);

    if (parent)
      GetRoot()->GetMotion()->force -= GetMaxThrust() * GetOutput() * GetLook();

    color = activation *
      Mix(Supertyped.type->GetColor(), kBoostColor, GetOvercharge());
    light->color = color;
  }

  void Thrust(V3 const& dir) {
    thrust = Saturate(2.0f * Saturate(-Dot(GetLook(), dir)) - 0.5f);
  }

  void ThrustAngular(V3 const& dir, float amount) {
    float torque = GetMaxTorque();
    parent->GetMotion()->torque +=
      Normalize(dir) * torque * Saturate(amount);
  }
};

#endif
