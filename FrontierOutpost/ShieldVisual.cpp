#include "Shield.h"

#include "DrawState.h"
#include "Model.h"
#include "RenderStyle.h"
#include "ShaderInstance.h"

/* How Shield objects are drawn: the client's half of Shield.cpp (ADR-016). */

namespace {
  static Shader gShader;
  static ShaderInstance gShaderInstance;

  void OnLoad() {
    gShader = Shader_Create("npm.jsl", "shield.jsl");
    gShaderInstance = ShaderInstance_Create(gShader);
    (*gShaderInstance)
      (RenderStateSwitch_BlendModeAdditive)
      (RenderStateSwitch_CullModeDisabled)
      (RenderStateSwitch_ZWritableOff);
  } static bool l = RegisterLoader(OnLoad);
}

namespace {
  struct RenderComponent : public RenderableT {
    DERIVED_TYPE(RenderComponent, RenderableT)
    Shield* self;

    RenderComponent(Shield* self) :
      self(self)
      {}

    Bound3 GetBound() const {
      return self->mesh ? self->mesh->GetBound() : Bound3(0);
    }

    Mesh GetCollisionMesh() const {
      return self->mesh ? self->mesh->Clone() : nullptr;
    }

    size_t GetHash() const {
      return (size_t)self->mesh;
    }

    short GetVersion() const {
      return self->mesh ? self->mesh->GetVersion() : (short)-1;
    }

    void Render(DrawState* state) const {
      if (!self->mesh)
        return;

      static std::vector<float> ages;
      static std::vector<V3> positions;
      ages.clear();
      positions.clear();

      for (size_t i = 0; i < self->hitAge.size(); ++i) {
        float thisAge = self->hitAge[i];
        if (thisAge > kMaxAge)
          continue;
        ages.push_back(thisAge);
        positions.push_back(self->hitPosition[i]);
      }

      /* Only draw the shield if there are active hits on it. */
      if (ages.size()) {
        gShader->SetFloatArray("hitAge", ages.data(), ages.size());
        gShader->SetFloat3Array("hitPos", positions.data(), positions.size());
        gShader->SetInt("activeHits", ages.size());
        RenderStyle const& style = RenderStyle_Get();
        style->SetShader(gShaderInstance);
        style->Render(self->mesh);
      }
    }

    V3 Sample() const {
      return self->mesh ? self->mesh->Sample() : V3(0);
    }
  };

  void Create(ObjectT* object, Renderable& visual) {
    visual = new RenderComponent((Shield*)object);
  }

  bool registered = (Game::RegisterVisual("Shield", Create), true);
}
