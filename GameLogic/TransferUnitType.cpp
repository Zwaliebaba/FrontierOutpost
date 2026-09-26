#include "Items.h"

#include "Constants.h"
#include "Icons.h"
#include "Materials.h"
#include "Objects.h"
#include "AttributeCapability.h"
#include "AttributeIcon.h"
#include "Mass.h"
#include "Metatype.h"
#include "Name.h"
#include "Offset.h"
#include "PowerDrain.h"
#include "Range.h"
#include "AttributeRenderable.h"
#include "AttributeScale.h"
#include "AttributeValue.h"

#include "Script.h"

typedef
    Attribute_Capability
  < Attribute_Icon
  < Attribute_Mass
  < Attribute_Metatype
  < Attribute_Name
  < Attribute_Offset
  < Attribute_PowerDrain
  < Attribute_Range
  < Attribute_Renderable
  < Attribute_Scale
  < Attribute_Value
  < ItemWrapper<ItemType_TransferUnitType>
  > > > > > > > > > > >
  TransferUnitTypeBase;

AutoClassDerivedEmpty(TransferUnitType, TransferUnitTypeBase)
  DERIVED_TYPE_EX(TransferUnitType)

  SocketType GetSocketType() const {
    return SocketType_Turret;
  }

  Object Instantiate(ObjectT* parent) {
    Object turret = Item_TurretType(1, kPi)->Instantiate();
    turret->Plug(Object_TransferUnit(this));
    return turret;
  }
};

DERIVED_IMPLEMENT(TransferUnitType)

DefineFunction(Item_TransferUnitType) {
  static Renderable renderable;
  if (!renderable)
    ScriptFunction_Load("Item/TransferUnitType:Generate")->Call(renderable);

  Mass mass = Constant_ValueToMass(args.value, args.compactness);
  float powerDrain = Constant_ValueToPowerDrain(args.value, args.efficiency);
  float output = Constant_ValueToOutput(args.value, args.rate);

  Reference<TransferUnitType> self = new TransferUnitType;
  self->capability = Capability_Transference(output);
  self->icon = Icon_Task_Mine();
  self->mass = mass;
  self->metatype = Item_TransferUnitType_Args(args);
  self->name = "Transfer Unit";
  self->offset = 0;
  self->powerDrain = powerDrain;
  self->range = 5.0f * Constant_RangeRatio(args.range);
  self->renderable = renderable;
  self->scale = 1.0f;//Constant_MassToScale(mass);
  self->value = args.value;
  return self;
}
