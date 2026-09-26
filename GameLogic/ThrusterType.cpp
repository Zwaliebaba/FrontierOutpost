#include "Items.h"

#include "Constants.h"
#include "Materials.h"
#include "Objects.h"
#include "AttributeCapability.h"
#include "AttributeColor.h"
#include "AttributeIcon.h"
#include "AttributeIntegrity.h"
#include "Mass.h"
#include "Metatype.h"
#include "Name.h"
#include "Offset.h"
#include "PowerDrain.h"
#include "AttributeRenderable.h"
#include "AttributeScale.h"
#include "AttributeValue.h"

#include "Script.h"

#include "Glyphs.h"

const float kThrustMult = 100;

const Icon kIcon = Icon_Create()
  ->Add(Glyph_Arc(0, 0.9f, 0.1f, 1, 1, 0.5f, 0.125f))
  ->Add(Glyph_LineFade(V2(-0.5f,  0.5f), V2(0.25f,  0.5f), 1, 1))
  ->Add(Glyph_LineFade(V2(-1.0f,  0.0f), V2(0.50f,  0.0f), 1, 1))
  ->Add(Glyph_LineFade(V2(-0.5f, -0.5f), V2(0.25f, -0.5f), 1, 1));

typedef
    Attribute_Capability
  < Attribute_Color
  < Attribute_Icon
  < Attribute_Integrity
  < Attribute_Mass
  < Attribute_Metatype
  < Attribute_Name
  < Attribute_Offset
  < Attribute_PowerDrain
  < Attribute_Renderable
  < Attribute_Scale
  < Attribute_Value
  < ItemWrapper<ItemType_ThrusterType>
  > > > > > > > > > > > >
  ThrusterTypeBase;

AutoClassDerivedEmpty(ThrusterType, ThrusterTypeBase)
  DERIVED_TYPE_EX(ThrusterType)

  SocketType GetSocketType() const {
    return SocketType_Thruster;
  }

  Object Instantiate(ObjectT* object) {
    return Object_Thruster(this, object);
  }
};

DERIVED_IMPLEMENT(ThrusterType)

DefineFunction(Item_ThrusterType) {
  static Renderable renderable;
  if (!renderable)
    ScriptFunction_Load("Item/ThrusterType:Generate")->Call(renderable);

  Mass mass = Constant_ValueToMass(args.value, args.compactness);
  Health integrity = Constant_ValueToIntegrity(args.value, args.integrity);
  float powerDrain = Constant_ValueToPowerDrain(args.value, args.efficiency);
  float output = kThrustMult * Constant_ValueToOutput(args.value, args.rate);

  Reference<ThrusterType> self = new ThrusterType;
  self->capability = Capability_Motion(output);
  self->color = Color(1.0f, 0.4f, 0.1f);
  self->icon = kIcon;
  self->integrity = integrity;
  self->mass = mass;
  self->metatype = Item_ThrusterType_Args(args);
  self->name = "Thruster";
  self->offset = V3(0, 0, 0.5f);
  self->powerDrain = powerDrain;
  self->renderable = renderable;
  self->scale = 1;
  self->value = args.value;
  return self;
}
