#include "ClientPresentation.h"

#include "Interior.h"
#include "Object.h"
#include "Particles.h"

#include "DrawState.h"
#include "HashMap.h"
#include "LteMath.h"
#include "ParticleSystem.h"
#include "Renderable.h"
#include "RenderStyle.h"
#include "StackFrame.h"
#include "Transform.h"
#include "View.h"

#include "Presentation.h"

/* The client's half of Drawable.cpp, Interior.cpp and GraphicsEffects.cpp: how game objects and
   interiors are drawn, the particles each interior keeps, and the particle effects the game sets
   off (ADR-016). The bodies are the game's own, moved here as they were. */

namespace {
  struct ClientPresentation : public Game::Presentation {
    /* Each interior's particles, by its Interior component. */
    HashMap<void const*, ParticleSystem> particles;

    ParticleSystem const& Particles(void const* interior) {
      ParticleSystem& ps = particles[interior];
      if (!ps)
        ps = ParticleSystem_Create();
      return ps;
    }

    void DrawObject(ObjectT* self, RenderableT* renderable, DrawState* state) override {
      RenderStyle_Get()->SetTransform(self->GetTransform());
      // DrawState_Push("objectRadius", self->GetRadius());
      renderable->Render(state);
      // DrawState_Pop("objectRadius");
    }

    void DrawInterior(ObjectT* self, DrawState* state) override {
      if (state->visible[0] == self) {
        RenderStyle_Get()->SetTransform(Transform_Identity());
        self->OnDrawInterior(state);
      }
    }

    V3D ViewPosition(DrawState* state) override {
      return state->view->transform.pos;
    }

    void BeginInteriorUpdate(void const* interior, float dt) override {
      ParticleSystem const& ps = Particles(interior);
      ParticleSystem_Push(ps);

      FRAME("Particle Update")
        ps->Run(dt);
    }

    void EndInteriorUpdate(void const* interior) override {
      ParticleSystem_Pop(Particles(interior));
    }

    void ForgetInterior(void const* interior) override {
      particles.erase(interior);
    }

    void BeamHit(
      Position const& origin,
      V3 const& baseVelocity,
      float scale,
      V3 const& color) override
    {
      V3 velocity = baseVelocity + scale * SampleSphere();
      float life = Rand(1.0f, 2.0f);
      ParticleSystem_Add(
        ParticleSystem_Get(),
        Particle_Firefly(),
        origin,
        velocity,
        Rand(0.5f, 1) * scale,
        life,
        color);
    }

    void ParticleFirefly(
      Position const& origin,
      V3 const& velocity,
      V3 const& color,
      float size,
      float lifeTime) override
    {
      ParticleSystem_Add(
        ParticleSystem_Get(),
        Particle_Firefly(),
        origin,
        velocity,
        size,
        lifeTime,
        color);
    }

    void SmallPlume(
      Position const& origin,
      V3 const& baseVelocity,
      V3 const& color,
      float size) override
    {
      ParticleSystem const& ps = ParticleSystem_Get();
      for (size_t i = 0; i < 15; ++i) {
        V3 p = origin + 0.5f * size * SampleSphere();
        V3 v = baseVelocity + 8.0f * size * SampleSphere();
        V3 c = color * RandV3(1.0f, 1.2f);
        float life = Rand(0.25f, 0.5f);

        ParticleSystem_Add(
          ps,
          Particle_Fire(),
          p,
          v,
          (1.0f + RandExp()) * size,
          life,
          c);
      }
    }
  };

  /* Never destroyed: interiors let their particles go from their destructors, which can run
     after every static has been destroyed. */
  ClientPresentation& Get() {
    static ClientPresentation* presentation = new ClientPresentation;
    return *presentation;
  }

  bool installed = (Game::SetPresentation(&Get()), true);
}

ParticleSystem const& ClientPresentation_Particles(ComponentInterior const* interior) {
  return Get().Particles(interior);
}
