#include "Visual.h"

#include "DrawState.h"
#include "Meshes.h"
#include "Model.h"
#include "Renderable.h"
#include "ShaderInstance.h"

/* How Wormhole objects are drawn: the client's half of Wormhole.cpp (ADR-016). One model, shared by
   every wormhole, made when the first one is drawn. */

namespace {
  Renderable GetWormholeModel() {
    static Renderable model;
    if (!model) {
      ShaderInstance ss = ShaderInstance_Create("npm.jsl", "wormhole.jsl");
      (*ss)(RenderStateSwitch_BlendModeAdditive);
      DrawState_Link(ss);
      model = (Renderable)Model_Create()
        ->Add(Mesh_BoxSphere(5, true)->ReverseWinding(), ss);
    }
    return model;
  }

  RenderableT* Create(ObjectT*) {
    return GetWormholeModel().t;
  }

  bool registered = (Game::RegisterVisual("Wormhole", Create), true);
}
