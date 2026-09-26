#include "Visual.h"

#include "Renderables.h"

#include "Renderable.h"

/* The seeded renderables game code asks for by kind: an asteroid's shape and a system's starfield
   (ADR-016). Renderable_Asteroid and Renderable_Starfield are the client's, and scripts call them
   by those names as before. */

namespace {
  void Asteroid(uint32 seed, uint32, Renderable& visual) {
    visual = Renderable_Asteroid(seed);
  }

  void Starfield(uint32 seed, uint32 starCount, Renderable& visual) {
    visual = Renderable_Starfield(seed, starCount);
  }

  bool registered =
    (Game::RegisterSeededVisual("Asteroid", Asteroid),
     Game::RegisterSeededVisual("Starfield", Starfield), true);
}
