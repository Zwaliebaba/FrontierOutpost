#include "Explosion.h"

#include "DrawState.h"
#include "Model.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"

/* How Explosion objects are drawn: the client's half of Explosion.cpp (ADR-016). */

namespace {
  struct RenderComponent : public RenderableT {
    DERIVED_TYPE(RenderComponent, RenderableT)
    Explosion* self;

    RenderComponent(Explosion* self) :
      self(self)
      {}

    Bound3 GetBound() const {
      return Bound3(V3(-1), V3(1));
    }

    void Render(DrawState* state) const {
      if (self->age < 0)
        return;

      /* Distance culling. */
      const float cullDistance = 1000;
      Transform const& transform = self->GetTransform();

      if (Length(state->view->transform.pos - transform.pos) > cullDistance * self->GetRadius())
        return;

      static Model model[ExplosionType_SIZE];
      static ShaderInstance ss[ExplosionType_SIZE];

      if (!model[0]) {
        char const* shaderPath[ExplosionType_SIZE] = {
          "explosion.jsl",
          "shield_explosion.jsl",
        };

        for (int i = 0; i < ExplosionType_SIZE; ++i) {
          Shader shader = Shader_Create("billboard_soft.jsl", shaderPath[i]);
          ss[i] = ShaderInstance_Create(shader);
          (*ss[i])
            ("zOffset", 30.0f)
            (RenderStateSwitch_BlendModeAdditive)
            (RenderStateSwitch_ZWritableOff);
          DrawState_Link(ss[i]);
          model[i] = Model_Create()->Add(Mesh_Billboard(), ss[i]);
        }
      }

      const Color kExplosionColor[ExplosionType_SIZE][2] = {
        { Color(1.0f, 0.2f, 0.1f), Color(1.8f, 0.8f, 0.5f) },
        { Color(0.1f, 0.2f, 1.0f), Color(0.3f, 0.8f, 1.8f) },
      };

      (*ss[self->type])
        ("billboardSize", 30.0f * transform.scale.GetMax())
        ("color1", kExplosionColor[self->type][0])
        ("color2", kExplosionColor[self->type][1])
        ("opacity", self->GetOpacity())
        ("age", self->age);

      RenderStyle_Get()->SetTransform(transform);
      model[self->type]->Render(state);
    }
  };

  void Create(ObjectT* object, Renderable& visual) {
    visual = new RenderComponent((Explosion*)object);
  }

  bool registered = (Game::RegisterVisual("Explosion", Create), true);
}
