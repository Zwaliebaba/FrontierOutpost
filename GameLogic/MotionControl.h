#ifndef Component_MotionControl_h__
#define Component_MotionControl_h__

#include "ComponentCommon.h"
#include "Item.h"
#include "AutoClass.h"
#include "SDF.h"
#include "Vector.h"

AutoClass(ComponentMotionControl,
  Vector<SDF>, elements)

  ComponentMotionControl() {}

  LT_API void Run(ObjectT* self, UpdateState& state);
};

AutoComponent(MotionControl)
  void OnUpdate(UpdateState& s) {
    MotionControl.Run(this, s);
    BaseT::OnUpdate(s);
  }
};

#endif
