#include "Renderables.h"

#include "Materials.h"

#include "LteMath.h"
#include "Model.h"
#include "SDFs.h"
#include "SDFMesh.h"

const size_t kUniqueModels = 3;

namespace {
  Renderable Generate(Renderable_Asteroid_Args const& args) {
    static Renderable models[kUniqueModels];
    if (!models[0]) {
      for (size_t i = 0; i < kUniqueModels; ++i) {
        SDF d = SDF_Radial(
          SDF_FractalWorley(Rand(1, 1000), 6, 2.6f), 0.0f, 2.0f);
        models[i] = (Renderable)Model_Create()
          ->Add(SDFMesh_Create(d), Material_Rock());
      }
    }

    return models[args.seed % kUniqueModels];
  }
}

DefineFunction(Renderable_Asteroid) {
  return Generate(args);
}
