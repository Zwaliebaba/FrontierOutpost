#ifndef Component_Crew_h__
#define Component_Crew_h__

#include "ComponentCommon.h"
#include "Item.h"
#include "AutoClass.h"
#include "Vector.h"

AutoClass(ComponentCrew,
  Vector<Item>, elements)

  ComponentCrew() {}
};

AutoComponent(Crew)
};

#endif
