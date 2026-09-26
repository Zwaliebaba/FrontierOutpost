#include "Visual.h"

#include "Renderables.h"

#include "Renderable.h"

/* The seeded renderables game code asks for by kind: an asteroid's shape (ADR-016).
   Renderable_Asteroid is the client's, and scripts call it by that name as before. */

namespace {
  void Asteroid(uint32 seed, uint32, Renderable& visual) {
    visual = Renderable_Asteroid(seed);
  }

  bool registered = (Game::RegisterSeededVisual("Asteroid", Asteroid), true);
}
