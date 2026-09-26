#include "Objects.h"

#include "BoundingBox.h"
#include "Cargo.h"
#include "Collidable.h"
#include "Cullable.h"
#include "Database.h"
#include "Drawable.h"
#include "Motion.h"
#include "Nameable.h"
#include "Orientation.h"

#include "Item.h"
#include "Materials.h"

#include "LteMath.h"
#include "Model.h"
#include "Pool.h"
#include "SDFs.h"
#include "SDFMesh.h"

Renderable GetPodModel() {
  static Renderable model;
  if (!model)
    model = (Renderable)Model_Create()
      ->Add(SDFMesh_Create(SDF_RoundBox(0, 1, 0.1f)), Material_Metal());
  return model;
}

typedef ObjectWrapper
  < Component_BoundingBox
  < Component_Cargo
  < Component_Collidable
  < Component_Cullable
  < Component_Database
  < Component_Drawable
  < Component_Motion
  < Component_Nameable
  < Component_Orientation
  < ObjectWrapperTail<ObjectType_Pod>
  > > > > > > > > > >
  PodBaseT;

AutoClassDerived(Pod, PodBaseT,
  Item, type,
  Quantity, quantity)

  DERIVED_TYPE_EX(Pod)
  POOLED_TYPE

  Pod() {
    Drawable.renderable = GetPodModel;
  }

  virtual bool CanCollide(ObjectT const* other) const {
    return other->GetCargo();
  }

  virtual void OnCollide(
    ObjectT* self,
    ObjectT* other,
    Position const& selfLocation,
    Position const& otherLocation)
  {
    Pointer<ComponentCargo> myInv = self->GetCargo();
    while (myInv->elements.size()) {
      Item item = myInv->elements.begin()->first;
      Quantity count = myInv->elements.begin()->second;
      if (other->AddItem(item, count))
        self->RemoveItem(item, count);
      else
        return;
    }

    Delete();
  }
};

DERIVED_IMPLEMENT(Pod)

DefineFunction(Object_Pod) {
  Reference<Pod> self = new Pod;
  self->SetScale(1);
  self->Cargo.capacity = args.capacity;
  return self;
}
