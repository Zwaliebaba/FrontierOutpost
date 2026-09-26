#include "PlanetType.h"

#include "LteMath.h"
#include "RNG.h"
#include "Renderable.h"
#include "Script.h"
#include "StackFrame.h"

#include "Glyphs.h"

#include "Visual.h"

DERIVED_IMPLEMENT(PlanetType)

DefineFunction(Item_PlanetType) { AUTO_FRAME;
  RNG rg = RNG_MTG(args.seed);

  Reference<PlanetType> self = new PlanetType;
  self->docks.push(Bound3(V3(-1), V3(1)));
  self->dockCapacity = -1;
  ScriptFunction_Load("Icons:Planet")->Call(self->icon);
  self->name = "Planet";
  self->scale = 100000;
  self->seed = args.seed;
  self->atmoDensity = rg->GetFloat(1.0f, 1.0f);
  self->atmoTint = V3(1);
  self->cloudLevel = rg->GetFloat(-0.2f, 0.15f);

  float desat = rg->GetFloat(0.75f, 1);
  self->color1 = Desaturate(rg->GetV3(0, 0.25f), desat);
  self->color2 = Desaturate(rg->GetV3(0.25f, 0.5f), desat);
  self->wavelength = V3(1) / Pow(V3(0.66f, 0.53f, 0.4f) + rg->GetV3(0, -0.1f), 4.0f);

  /* The client's look, where there is a client (FrontierOutpost/PlanetTypeVisual.cpp, ADR-016). */
  Game::CreateItemVisual("PlanetType", self, 0, self->renderable);
  return self;
}
