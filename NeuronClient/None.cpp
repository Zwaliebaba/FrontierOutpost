#include "Compositors.h"

#include "LteMath.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Shader.h"
#include "Texture2D.h"

namespace {
  AutoClassDerivedEmpty(CompositorNone, CompositorT)
    Shader shader;

    CompositorNone() {
      shader = Shader_Create("identity.jsl", "ui/none.jsl");
    }

    void Composite(Texture2D const& layer, Mesh const& surface) {
      RendererState s(BlendMode::Alpha, CullMode::Backface, false, false);
      (*shader)("layer", layer);
      Renderer_SetShader(*shader);
      surface->Draw();
    }
  };
}

DefineFunction(Compositor_None) {
  return new CompositorNone;
}
