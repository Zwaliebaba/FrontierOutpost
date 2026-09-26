#ifndef Component_Pluggable_h__
#define Component_Pluggable_h__

#include "ComponentCommon.h"
#include "Object.h"
#include "AutoClass.h"

AutoClass(ComponentPluggable,
  uint, index,
  float, powerIn,
  float, powerOut,
  float, powerRequest,
  float, priority)

  ComponentPluggable() :
    index(0),
    powerIn(0),
    powerOut(0),
    powerRequest(0),
    priority(1)
    {}

  LT_API Socket const& GetSocket(ObjectT const* self) const;

  LT_API void UpdateJoint(ObjectT* self) const;
};

AutoComponent(Pluggable)
  float GetPowerFraction() const {
    return Pluggable.powerRequest > 0
      ? Pluggable.powerIn / Pluggable.powerRequest
      : 1;
  }
};

#endif
