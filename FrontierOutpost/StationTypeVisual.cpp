#include "StationType.h"

#include "Materials.h"

#include "Mesh.h"
#include "Model.h"
#include "PlateMesh.h"
#include "Renderable.h"
#include "Script.h"
#include "SDFs.h"
#include "SDFMesh.h"

#include "Visual.h"

/* How a StationType looks: the client's half of StationType.cpp (ADR-016). The hull is also what
   the station collides with; without a client there is none. */

namespace {
  void Interior(ItemT*, uint32, Renderable& interiorModel) {
    SDF interior = SDF_Shell(0, 1, 0.1f)
      ->Subtract(SDF_Cylinder(0, V3(0, 0, 1), 0.1f));

    interiorModel =
      (Renderable)Model_Create()->Add(SDFMesh_Create(interior), Material_Rock());
  }

  void Hull(ItemT*, uint32 seed, Renderable& renderable) {
    PlateMesh pm;
    ScriptFunction_Load("Item/StationType/Generate:Main")
      ->Call(pm, (int)seed);
    Mesh mesh = pm->GetMesh();
    Mesh_ComputeOcclusion(mesh);
    renderable = (Renderable)Model_Create()->Add(mesh, Material_Metal());
  }

  bool registered =
    (Game::RegisterItemVisual("StationTypeInterior", Interior),
     Game::RegisterItemVisual("StationType", Hull), true);
}
