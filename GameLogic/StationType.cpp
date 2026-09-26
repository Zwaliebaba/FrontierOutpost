#include "StationType.h"

#include "Constants.h"
#include "Icons.h"
#include "Materials.h"
#include "Objects.h"

#include "Bound.h"
#include "LteMath.h"
#include "Mesh.h"
#include "Model.h"
#include "PlateMesh.h"
#include "RNG.h"
#include "Ray.h"
#include "Script.h"
#include "SDFs.h"
#include "SDFMesh.h"
#include "ShaderInstance.h"
#include "StackFrame.h"

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

  SDF interior = SDF_Shell(0, 1, 0.1f)
    ->Subtract(SDF_Cylinder(0, V3(0, 0, 1), 0.1f));

  self->interiorModel =
    (Renderable)Model_Create()->Add(SDFMesh_Create(interior), Material_Rock());

  self->metatype = Item_StationType_Args(args);

  self->name = "Station";

  self->scale = Constant_MassToScale(mass);

  PlateMesh pm;
  ScriptFunction_Load("Item/StationType/Generate:Main")
    ->Call(pm, (int)rg->GetInt());
  Mesh mesh = pm->GetMesh();
  Mesh_ComputeOcclusion(mesh);
  self->renderable = (Renderable)Model_Create()->Add(mesh, Material_Metal());

  self->value = args.value;
  return self;
}
