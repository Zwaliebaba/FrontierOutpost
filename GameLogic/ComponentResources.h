#ifndef Component_Resources_h__
#define Component_Resources_h__

#include "ComponentCommon.h"
#include "AutoClass.h"
#include "Distribution.h"

AutoClass(ComponentResources,
  Distribution<Item>, elements)

  ComponentResources() {}
};

AutoComponent(Resources)
};

#endif
