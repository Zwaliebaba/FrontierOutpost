#include "Items.h"

#include "Constants.h"
#include "Objects.h"
#include "AttributeIcon.h"
#include "Metatype.h"
#include "Name.h"
#include "PowerDrain.h"
#include "Range.h"
#include "AttributeValue.h"

#include "Script.h"

#include "Glyphs.h"

typedef
    Attribute_Icon
  < Attribute_Metatype
  < Attribute_Name
  < Attribute_PowerDrain
  < Attribute_Range
  < Attribute_Value
  < ItemWrapper<ItemType_ScannerType>
  > > > > > >
  ScannerTypeBase;

AutoClassDerivedEmpty(ScannerType, ScannerTypeBase)
  DERIVED_TYPE_EX(ScannerType)

  SocketType GetSocketType() const {
    return SocketType_Generator;
  }

  Object Instantiate(ObjectT* parent) {
    return Object_Scanner(this);
  }
};

DERIVED_IMPLEMENT(ScannerType)

DefineFunction(Item_ScannerType) {
  Reference<ScannerType> self = new ScannerType;
  ScriptFunction_Load("Icons:Scanner")->Call(self->icon);
  self->metatype = Item_ScannerType_Args(args);
  self->name = "Scanner";
  self->powerDrain = 1.0f;
  self->range = Constant_RangeRatio(args.range);
  self->value = args.value;
  return self;
}
