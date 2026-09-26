#include "Visual.h"

#include "Materials.h"

#include "Model.h"
#include "Renderable.h"
#include "SDFs.h"
#include "SDFMesh.h"

/* How Pod objects are drawn: the client's half of Pod.cpp (ADR-016). One model, shared by every
   pod, made when the first one is drawn. */

namespace {
  Renderable GetPodModel() {
    static Renderable model;
    if (!model)
      model = (Renderable)Model_Create()
        ->Add(SDFMesh_Create(SDF_RoundBox(0, 1, 0.1f)), Material_Metal());
    return model;
  }

  RenderableT* Create(ObjectT*) {
    return GetPodModel().t;
  }

  bool registered = (Game::RegisterVisual("Pod", Create), true);
}
