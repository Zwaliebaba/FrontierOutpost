#ifndef Item_StationType_h__
#define Item_StationType_h__

#include "Items.h"
#include "AttributeCapability.h"
#include "Docks.h"
#include "AttributeIcon.h"
#include "AttributeIntegrity.h"
#include "Mass.h"
#include "Metatype.h"
#include "Name.h"
#include "AttributeRenderable.h"
#include "AttributeScale.h"
#include "AttributeSockets.h"
#include "AttributeValue.h"

typedef
    Attribute_Capability
  < Attribute_Docks
  < Attribute_Icon
  < Attribute_Integrity
  < Attribute_Mass
  < Attribute_Metatype
  < Attribute_Name
  < Attribute_Renderable
  < Attribute_Scale
  < Attribute_Sockets
  < Attribute_Value
  < ItemWrapper<ItemType_StationType>
  > > > > > > > > > > >
  StationTypeBase;

AutoClassDerivedEmpty(StationType, StationTypeBase)
  DERIVED_TYPE_EX(StationType)
  Renderable interiorModel;

  LT_API Object Instantiate(ObjectT* parent = 0);
};

#endif
