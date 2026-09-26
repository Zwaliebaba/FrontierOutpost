#include "System.h"

#include "Messages.h"
#include "Universe.h"

#include "Grammar.h"
#include "LteMath.h"
#include "RNG.h"
#include "Script.h"
#include "StackFrame.h"

#include "Debug.h"

namespace {
  V3 GenerateStarColor(RNG const& rng) {
    return V3(1.0f) - 0.5f * Log(rng->GetV3(0, 1)) * V3(1.0f, 0.5f, 1.0f);
  }
}

DERIVED_IMPLEMENT(System)

DefineFunction(Object_System) { AUTO_FRAME;
  Reference<System> self = new System;

  RNG rng = RNG_MTG(args.seed);
  self->Seeded.seed = args.seed;
  self->SetPos(args.position);

  /* Create the central star. */ {
    self->star = Object_Star(GenerateStarColor(rng));
    self->star->SetPos(Spherical(60000000, 1.25f * kPi2, 0.0f));
    self->AddInterior(self->star);
    self->Initialize();
  }

  /* Create the dust. */ {
    self->AddInterior(Object_DustFlecks());
  }

  return self;
}
