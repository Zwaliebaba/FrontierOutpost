#include "Objects.h"
#include "Items.h"

#include "Drawable.h"
#include "Mineable.h"
#include "Orientation.h"

#include "Player.h"
#include "GameSettings.h"
#include "Name.h"
#include "AttributeValue.h"

#include "LteMath.h"

typedef ObjectWrapper
  < Component_Drawable
  < Component_Orientation
  < ObjectWrapperTail<ObjectType_Drone>
  > > >
  DroneProspectingBaseT;

AutoClassDerivedEmpty(DroneProspecting, DroneProspectingBaseT)
  DERIVED_TYPE_EX(DroneProspecting)
  
  DroneProspecting() {}

  DroneProspecting(ObjectT* parent) {
    parent->AddChild(this);
    SetScale(2);
  }

  void OnUpdate(UpdateState& state) {}
};

DERIVED_IMPLEMENT(DroneProspecting)

typedef
    Attribute_Name
  < Attribute_Value
  < ItemWrapper<ItemType_DroneType>
  > >
  DroneProspectingTypeBaseT;

AutoClassDerivedEmpty(DroneProspectingType, DroneProspectingTypeBaseT)
  DERIVED_TYPE_EX(DroneProspectingType)

  Object Instantiate(ObjectT* parent) {
    return new DroneProspecting(parent);
  }
};

DefineFunction(Item_DroneProspectingType) {
  return new DroneProspectingType;
}

DERIVED_IMPLEMENT(DroneProspectingType)
