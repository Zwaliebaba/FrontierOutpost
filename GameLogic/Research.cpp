#include "GameTasks.h"

#include "Events.h"
#include "Icons.h"
#include "Items.h"
#include "Object.h"
#include "Blueprint.h"

#include "LteMath.h"
#include "Pool.h"
#include "StackFrame.h"

#include "SoundEngine.h"

namespace {
  /* TODO : Output of research? */

  AutoClassDerived(TaskResearch, TaskT, Task_Research_Args, args)
    DERIVED_TYPE_EX(TaskResearch)
    POOLED_TYPE

    TaskResearch() {}

    float GetDuration() const {
      return args.blueprint->GetValue();
    }

    Icon GetIcon() const {
      return Icon_Task_Research();
    }

    String GetName() const {
      return "Research";
    }

    String GetNoun() const {
      return "Research Lab";
    }

    Capability GetRateFactor() const {
      return Capability_Research(1);
    }

    void OnUpdate(Object const& self, float dt, Data& data) { AUTO_FRAME;
      if (10.0 * RandExp() < dt) {
        Item item = Item_Blueprint_Derived(args.blueprint.t);
        self->GetRoot()->AddItem(item, 1);
        Sound_Play3D("techlab/complete.wav", self, 0, 1, 0.05f);

        Player const& owner = self->GetOwner();
        if (owner)
          owner->AddLogMessage("Discovered [" + item->GetName() + "]");
      }
    }
  };
}

DefineFunction(Task_Research) {
  return new TaskResearch(args);
}
