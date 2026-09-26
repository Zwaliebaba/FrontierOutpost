#ifndef Component_Targets_h__
#define Component_Targets_h__

#include "ComponentCommon.h"
#include "Object.h"
#include "AutoClass.h"
#include "Vector.h"

AutoClass(ComponentTargets,
  Vector<Object>, elements)

  ComponentTargets() {}
};

AutoComponent(Targets)
};

#endif
