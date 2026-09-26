#include "RenderStyles.h"

#include "ModuleSettings.h"

#include "DrawState.h"
#include "Geometry.h"
#include "LteMath.h"
#include "Matrix.h"
#include "Meshes.h"
#include "Pointer.h"
#include "Renderer.h"
#include "ShaderInstance.h"
#include "View.h"

namespace {
  struct RenderStyleDefaultImpl : public RenderStyleT {
    ShaderInstance currentShader;
    bool blended;
    bool willRender;

    RenderStyleDefaultImpl(bool blended) :
      blended(blended),
      willRender(true)
      {}

    void OnBegin() {
      Renderer_PushBlendMode(blended ? BlendMode::Alpha : BlendMode::Disabled);
      Renderer_PushZBuffer(true);
      Renderer_PushZWritable(!blended);
      willRender = true;
    }

    void OnEnd() {
      Renderer_PopBlendMode();
      Renderer_PopZBuffer();
      Renderer_PopZWritable();
    }

    void Render(Geometry const& geometry) {
      if (willRender) {
        currentShader->Begin();
        geometry->Draw();
        currentShader->End();
      }
    }

    void SetShader(ShaderInstanceT* shader) {
      bool hasBlending = shader->HasBlending();
      if ((hasBlending && blended) || (!hasBlending && !blended)) {
        willRender = true;
        if (currentShader != shader) {
          currentShader = shader;
          /* Offered to every shader; only the materials read it. */
          Shader const& program = currentShader->GetShader();
          int const prepassIndex = program->QueryUniformLocation("prepass");
          if (prepassIndex >= 0)
            program->SetInt(prepassIndex, 0);
        }
        DrawState_Inject(currentShader->GetShader());
      } else {
        willRender = false;
      }
    }

    void SetTransform(Transform const& transform) {
      Renderer_SetWorldTransform(transform);
    }

    bool WillRender() const {
      return willRender;
    }
  };
}

RenderStyle RenderStyle_Default(bool blended) {
  return new RenderStyleDefaultImpl(blended);
}
