#include "Visual.h"

#include "Camera.h"
#include "Object.h"

#include "DrawState.h"
#include "LteMath.h"
#include "Meshes.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"
#include "Transform.h"

/* How DustFlecks objects are drawn: the client's half of DustFlecks.cpp (ADR-016). */

const uint kFleckCount = 1024;
const float kDistance = 1024;

namespace {
  void OnDraw(ObjectT*, DrawState*) {
    static ShaderInstance shader;
    static Mesh mesh;

    if (!shader) {
      shader = ShaderInstance_Create("billboard_axis_wrapped.jsl", "dustfleck.jsl");
      (*shader)
        (RenderStateSwitch_BlendModeAdditive)
        (RenderStateSwitch_ZWritableOff);
      DrawState_Link(shader);

      mesh = Mesh_Create();
      for (uint i = 0; i < kFleckCount; ++i)
        mesh->AddMesh(Mesh_Billboard()->Translate(SampleSphere() * kDistance));
    }

    V3 velocity = Camera_Get()->GetTarget()->GetVelocity();
    (*shader)
      ("axis", Normalize(velocity))
      ("size", V2(4.0f, 0.06f * Min(1000.0f, Length(velocity))));

    RenderStyle const& style = RenderStyle_Get();
    style->SetTransform(Transform_Identity());
    style->SetShader(shader);
    style->Render(mesh);
  }

  bool registered = (Game::RegisterDraw("DustFlecks", Game::DrawPhase::Object, OnDraw), true);
}
