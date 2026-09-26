#ifndef Component_Seeded_h__
#define Component_Seeded_h__

#include "ComponentCommon.h"
#include "AutoClass.h"

AutoClass(ComponentSeeded,
  uint, seed)

  ComponentSeeded() :
    seed(0)
    {}
};

AutoComponent(Seeded)
  uint GetSeed() const {
    return Seeded.seed;
  }
};

#endif
