#include "GameTasks.h"

#include "Events.h"
#include "Icons.h"
#include "Object.h"
#include "Blueprint.h"

#include "LteMath.h"
#include "Pool.h"
#include "StackFrame.h"

namespace {
  AutoClassDerived(TaskMint, TaskT,
    Task_Mint_Args, args)
    DERIVED_TYPE_EX(TaskMint)
    POOLED_TYPE

    TaskMint() {}

    float GetDuration() const {
      return args.blueprint->GetValue();
    }

    Icon GetIcon() const {
      return Icon_Disc();
    }

    String GetName() const {
      return "Mint Assembly Chip";
    }

    String GetNoun() const {
      return "Assembly Chip Lab";
    }

    Capability GetRateFactor() const {
      return Capability_Research(1);
    }

    void OnUpdate(Object const& self, float dt, Data& data) { AUTO_FRAME;
      if (RandExp() < dt) {
        Blueprint* bp = (Blueprint*)args.blueprint.t;
        self->GetRoot()->AddItem(bp->assemblyChip, 1);
      }
    }
  };
}

DefineFunction(Task_Mint) {
  return new TaskMint(args);
}
