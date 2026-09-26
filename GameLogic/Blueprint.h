#ifndef Item_Blueprint_h__
#define Item_Blueprint_h__

#include "Items.h"
#include "ItemWrapper.h"
#include "AttributeIcon.h"
#include "Name.h"
#include "AttributeValue.h"
#include "AutoClass.h"
#include "Data.h"
#include "Pointer.h"

typedef
    Attribute_Icon
  < Attribute_Name
  < Attribute_Value
  < ItemWrapper<ItemType_Blueprint>
  > > >
  BlueprintBase;

AutoClass(Modifier,
  String, attribute,
  float, multiplier)
  Modifier() {}
};

AutoClassDerived(Blueprint, BlueprintBase,
  Pointer<Blueprint>, parent,
  Data, metatype,
  Item, assemblyChip)

  DERIVED_TYPE_EX(Blueprint)

  Blueprint() {}
};

#endif
