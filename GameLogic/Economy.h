#ifndef Component_Economy_h__
#define Component_Economy_h__

#include "ComponentCommon.h"

#include "Capability.h"
#include "Item.h"
#include "Object.h"
#include "Player.h"

#include "Vector.h"

AutoClass(ComponentEconomy,
  Vector<Object>, nodes,
  Vector<Item>, items,
  Vector<Task>, tasks)

  ComponentEconomy() {}

  Item GetItem() const {
    return items.size() ? items.random() : nullptr;
  }

  LT_API void Run(ObjectT*, UpdateState&);
};

AutoComponent(Economy)
  void OnUpdate(UpdateState& s) {
    Economy.Run(this, s);
    BaseT::OnUpdate(s);
  }
};

#endif
