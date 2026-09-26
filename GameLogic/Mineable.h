#ifndef Component_Mineable_h__
#define Component_Mineable_h__

#include "ComponentCommon.h"
#include "Item.h"
#include "AutoClass.h"
#include "Vector.h"

AutoClass(ComponentMineable,
  Item, item,
  Quantity, quantity,
  V3, phase)

  ComponentMineable() :
    quantity(0),
    phase(0)
    {}
};

AutoComponent(Mineable)
};

#endif
