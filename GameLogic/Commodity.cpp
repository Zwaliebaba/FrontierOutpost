#include "ItemWrapper.h"
#include "AttributeIcon.h"
#include "Mass.h"
#include "Name.h"
#include "AttributeValue.h"

typedef
    Attribute_Icon
  < Attribute_Mass
  < Attribute_Name
  < Attribute_Value
  < ItemWrapper<ItemType_Commodity>
  > > > >
  CommodityBase;

AutoClassDerivedEmpty(Commodity, CommodityBase)
  DERIVED_TYPE_EX(Commodity)
};

DERIVED_IMPLEMENT(Commodity)

Item Item_Commodity(int id) {
  NOT_IMPLEMENTED
  return nullptr;
}
