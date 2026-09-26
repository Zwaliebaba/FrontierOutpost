#include "StationType.h"

#include "Constants.h"
#include "Icons.h"
#include "Objects.h"

#include "Bound.h"
#include "LteMath.h"
#include "Mesh.h"
#include "RNG.h"
#include "Ray.h"
#include "Renderable.h"
#include "Script.h"
#include "SDFs.h"
#include "StackFrame.h"

#include "Visual.h"

DERIVED_IMPLEMENT(StationType)

size_t kUniqueStationTypes = 1;

Object StationType::Instantiate(ObjectT* parent) {
  return Object_Station(this);
}

DefineFunction(Item_StationType) { AUTO_FRAME;
  RNG rg = RNG_MTG(args.seed);

  Mass capacity = Constant_ValueToCapacity(args.value, args.capacity);
  Health integrity = Constant_ValueToIntegrity(args.value, args.integrity);
  Mass mass = 10.0f * Constant_ValueToMass(args.value);

  Reference<StationType> self = new StationType;
  self->capability = Capability_Storage(capacity);

  V3 dockOffset = V3(0, 5, 2);
  self->dockCapacity = 100;

  self->docks.push(Bound3(V3(-1) + dockOffset, V3(1) + dockOffset));

  ScriptFunction_Load("Icons:Station")->Call(self->icon);

  self->integrity = integrity;

  /* The interior and the hull are the client's (FrontierOutpost/StationTypeVisual.cpp, ADR-016). */
  Game::CreateItemVisual("StationTypeInterior", self, 0, self->interiorModel);

  self->metatype = Item_StationType_Args(args);

  self->name = "Station";

  self->scale = Constant_MassToScale(mass);

  Game::CreateItemVisual("StationType", self, (uint32)(int)rg->GetInt(), self->renderable);

  self->value = args.value;
  return self;
}
