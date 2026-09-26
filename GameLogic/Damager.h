#ifndef Component_Damager_h__
#define Component_Damager_h__

#include "ComponentCommon.h"
#include "Object.h"
#include "WeaponType.h"
#include "AutoClass.h"

AutoClass(ComponentDamager,
  Reference<WeaponType>, type,
  Object, source)

  ComponentDamager() {}

  LT_API bool Hit(
    ObjectT* self,
    Object const& other,
    Position const& position,
    float dt);
};

AutoComponent(Damager)
};

#endif
