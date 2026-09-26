#include "Objects.h"

#include "Affectable.h"
#include "Asset.h"
#include "BoundingBox.h"
#include "Cargo.h"
#include "Collidable.h"
#include "Crew.h"
#include "Cullable.h"
#include "Database.h"
#include "Detectable.h"
#include "Drawable.h"
#include "Explodable.h"
#include "Integrity.h"
#include "Motion.h"
#include "MotionControl.h"
#include "Nameable.h"
#include "Orientation.h"
#include "Pilotable.h"
#include "Scriptable.h"
#include "Sockets.h"
#include "Supertyped.h"
#include "Targets.h"
#include "ComponentTasks.h"

#include "LteMath.h"

const uint kTrailLength = 64;

typedef ObjectWrapper
  < Component_Affectable
  < Component_Asset
  < Component_BoundingBox
  < Component_Cargo
  < Component_Collidable
  < Component_Crew
  < Component_Cullable
  < Component_Database
  < Component_Detectable
  < Component_Drawable
  < Component_Explodable
  < Component_Integrity
  < Component_Motion
  < Component_MotionControl
  < Component_Nameable
  < Component_Orientation
  < Component_Pilotable
  < Component_Scriptable
  < Component_Sockets
  < Component_Supertyped
  < Component_Targets
  < Component_Tasks
  < ObjectWrapperTail<ObjectType_Ship>
  > > > > > > > > > > > > > > > > > > > > > > >
  ShipBaseT;

AutoClassDerived(Ship, ShipBaseT,
  Object, trail)
  DERIVED_TYPE_EX(Ship)
  POOLED_TYPE
  
  Ship() {}

  float GetCullDistanceMult() const {
    return 1.0f;
  }

  Signature GetSignature() const {
    return Signature(GetRadius(), 8, 0.1f, 4);
  }

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);

#if 0
    if (!trail) {
      trail = Object_Trail(this, kTrailLength, V3(0.5f), 0.1f * GetRadius());
      trail->SetLocalTransform(Transform_Translation(GetLocalBound().GetFrontPoint()));
      trail->Update(state);
    }
#endif

    Motion.mass = GetSupertype()->GetMass() + Cargo.currentMass;
  }
};

DERIVED_IMPLEMENT(Ship)

DefineFunction(Object_Ship) {
  Reference<Ship> self = new Ship;
  self->SetSupertype(args.type);
  ScriptFunction_Load("Object/Ship:Init")->VoidCall(0, DataRef((Object)self));
  return self;
}
