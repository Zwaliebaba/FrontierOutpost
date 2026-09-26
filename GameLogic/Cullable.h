#ifndef Component_Cullable_h__
#define Component_Cullable_h__

#include "ComponentCommon.h"
#include "Object.h"
#include "AutoClass.h"

AutoClass(ComponentCullable,
  float, cullDistanceSquared)

  ComponentCullable() : cullDistanceSquared(0.0f) {}

  void Recompute(ObjectT const* self) const {
    Mutable(this)->cullDistanceSquared =
      500 * self->GetRadius() * self->GetCullDistanceMult();
    Mutable(this)->cullDistanceSquared *= cullDistanceSquared;
  }
};

AutoComponent(Cullable)
};

#endif
