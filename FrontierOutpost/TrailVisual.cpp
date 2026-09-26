#include "Trail.h"

#include "DrawState.h"
#include "ParticleSystem.h"
#include "Renderer.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"

/* How Trail objects are drawn: the client's half of Trail.cpp (ADR-016). */

namespace {
  struct RenderComponent : public RenderableT {
    DERIVED_TYPE(RenderComponent, RenderableT)
    Trail* self;

    RenderComponent(Trail* self) :
      self(self)
      {}

    void Render(DrawState* state) const {
      if (self->trail.size() < 2)
        return;

      static Shader shader;
      static ShaderInstance shaderState;
      if (!shader) {
        shader = Shader_Create("trail.jsl", "trail.jsl");
        shaderState = ShaderInstance_Create(shader);
        (*shaderState)
          (RenderStateSwitch_BlendModeAdditive)
          (RenderStateSwitch_ZWritableOff)
          ("maxAlpha", 1.0f);
      }

      const Distance cullDistance = 5000;
      Distance dist = Length(self->trail[0].p - state->view->transform.pos);
      if (dist > cullDistance * self->size)
        return;

      RenderStyle const& style = RenderStyle_Get();
      style->SetTransform(Transform_Identity());
      style->SetShader(shaderState);

      if (!style->WillRender())
        return;

      static Vector<Vertex> vertexArray;
      static Vector<ushort> indexArray;
      vertexArray.clear();
      indexArray.clear();

      V3 direction = 0;
      float totalLength = 0;

      for (size_t i = 0; i < self->trail.size(); ++i) {
        SegmentData const& segment = self->trail.GetRelative(i + 1);

        size_t indexOffset = vertexArray.size();
        Position const& p = segment.p;
        float const& opacity = segment.opacity;

        if (i + 1 < self->trail.size()) {
          SegmentData const& lastSegment = self->trail.GetRelative(i + 2);
          Position const& pLast = lastSegment.p;
          direction = pLast - p;
        }

        for (uint j = 0; j < 2; ++j) {
          Vertex v;
          v.p = p - state->view->transform.pos;
          v.n = direction;
          v.u = (j ? 1.0f : -1.0f);
          v.v = opacity;
          vertexArray.push(v);
        }

        if (i + 1 < self->trail.size()) {
          indexArray.push((short)(0 + indexOffset));
          indexArray.push((short)(1 + indexOffset));
          indexArray.push((short)(3 + indexOffset));
          indexArray.push((short)(0 + indexOffset));
          indexArray.push((short)(3 + indexOffset));
          indexArray.push((short)(2 + indexOffset));
        }
      
        totalLength += Length(direction);
      }

      (*shader)
        ("color", self->color)
        ("size", self->size)
        ("totalLength", totalLength);
      DrawState_Link(shader);

      shaderState->Begin();
      Renderer_DrawVertices(vertexArray, indexArray);
      shaderState->End();
    }
  };

  RenderableT* Create(ObjectT* object) {
    return new RenderComponent((Trail*)object);
  }

  bool registered = (Game::RegisterVisual("Trail", Create), true);
}
