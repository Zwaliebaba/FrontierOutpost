#include "Visual.h"

#include "Materials.h"

#include "DrawState.h"
#include "Meshes.h"
#include "Model.h"
#include "Renderable.h"

/* How WarpNode objects are drawn: the client's half of WarpNode.cpp (ADR-016). One model, shared
   by every warp node, made when the first one is drawn. */

namespace {
  Renderable GetModel() {
#if 0
    static Renderable model;
    if (!model) {
      ShaderInstance ss = ShaderInstance_Create("npm.jsl", "wormhole.jsl");
      (*ss)(RenderStateSwitch_BlendModeAdditive);
      DrawState_Link(ss);
      model = (Renderable)Model_Create()
        ->Add(Mesh_BoxSphere(5, true)
            ->ReverseWinding(), ss);
    }
    return model;
#else
    static Model model;
    if (!model)
      model = Model_Create()->Add(
        Mesh_BoxSphere(16, true)->SetU(1),
        Material_Ice());
    return model;
#endif
  }

  void Create(ObjectT*, Renderable& visual) {
    visual = GetModel();
  }

  bool registered = (Game::RegisterVisual("WarpNode", Create), true);
}
