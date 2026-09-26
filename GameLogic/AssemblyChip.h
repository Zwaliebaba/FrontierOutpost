#ifndef Item_AssemblyChip_h__
#define Item_AssemblyChip_h__

#include "Items.h"
#include "AttributeIcon.h"
#include "Name.h"
#include "AttributeValue.h"
#include "AutoClass.h"
#include "Vector.h"

typedef
    Attribute_Icon
  < Attribute_Name
  < Attribute_Value
  < ItemWrapper<ItemType_AssemblyChip>
  > > >
  AssemblyChipBase;

AutoClassDerived(AssemblyChip, AssemblyChipBase,
  Item, blueprint,
  Item, item,
  Vector<ItemQuantity>, requirements,
  float, duration)

  DERIVED_TYPE_EX(AssemblyChip)

  AssemblyChip() :
    duration(0)
    {}
};

#endif
