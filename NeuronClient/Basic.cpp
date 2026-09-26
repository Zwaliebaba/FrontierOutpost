#include "Compositors.h"

#include "LteMath.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Shader.h"
#include "Texture2D.h"

#include "FrameTimer.h"

namespace {
  AutoClassDerived(CompositorBasic, CompositorT,
    float, lines,
    float, noise,
    V3, gradeBlue,
    float, age)
    Shader shader;

    CompositorBasic() {}

    DefineInitializer {
      shader = Shader_Create("identity.jsl", "ui/basic.jsl");
    }

    void Composite(Texture2D const& layer, Mesh const& surface) {
      RendererState s(BlendMode::Alpha, CullMode::Backface, false, false);
      (*shader)
        ("age", age)
        ("layer", layer)
        ("linesMag", lines)
        ("gradeBlue", gradeBlue)
        ("noiseMag", noise)
        ("seed", Rand())
        ("size", V2(layer->GetWidth(), layer->GetHeight()));
      Renderer_SetShader(*shader);
      surface->Draw();
    }

    void Update() {
      age += FrameTimer_Get();
    }
  };
}

DefineFunction(Compositor_Basic) {
  return new CompositorBasic(args.lines, args.noise, args.gradeBlue, 0);
}
