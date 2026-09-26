#ifndef Component_Detectable_h__
#define Component_Detectable_h__

#include "ComponentCommon.h"
#include "AutoClass.h"

AutoClass(ComponentDetectable,
  Time, detectionTime)

  ComponentDetectable() :
    detectionTime(60000000)
    {}
};

AutoComponent(Detectable)
};

#endif
