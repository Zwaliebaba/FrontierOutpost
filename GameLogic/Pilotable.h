#ifndef Component_Pilotable_h__
#define Component_Pilotable_h__

#include "ComponentCommon.h"
#include "Player.h"
#include "AutoClass.h"

AutoClass(ComponentPilotable,
  Player, pilot)

  ComponentPilotable() {}

  LT_API void Run(ObjectT* self, UpdateState& state);
};

AutoComponent(Pilotable)
  void OnUpdate(UpdateState& s) {
    Pilotable.Run(this, s);
    BaseT::OnUpdate(s);
  }
};

#endif
