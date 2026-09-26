#include "GameTasks.h"

#include "Cargo.h"
#include "Interior.h"
#include "ComponentResources.h"

#include "Conditions.h"
#include "Events.h"
#include "Icons.h"
#include "Object.h"

#include "LteMath.h"
#include "Pool.h"
#include "StackFrame.h"

#include "Debug.h"

namespace {
  AutoClass(TaskPirateInstance,
    Object, target)

    TaskPirateInstance() {}
  };

  AutoClassDerived(TaskPirate, TaskT,
    Task_Pirate_Args, args)
    DERIVED_TYPE_EX(TaskPirate)
    POOLED_TYPE

    TaskPirate() {}

    bool CanAttack(Object const& self, Object const& target) const {
      return
        target->GetCargo() &&
        //target->GetCargo()->currentMass > 0 &&
        target->IsAlive() &&
        self->GetOwner() != target->GetOwner() &&
        !(target->GetOwner() && target->GetOwner()->IsHuman()); // TEMPCHANGE
    }

    float GetAlignment() const {
      return -1;
    }

    /* TODO */
    float GetDuration() const {
      return 1;
    }

    Icon GetIcon() const {
      return Icon_Crosshair();
    }

    String GetName() const {
      return "Pirate";
    }

    String GetNoun() const {
      return "Raider";
    }

    void GetOutput(Vector<ItemDelta>& outputs) const {
      Pointer<ComponentResources> resources = args.zone->GetResources();
      for (size_t i = 0; i < resources->elements.size(); ++i)
        outputs.push(ItemDelta(resources->elements.entries[i].key, nullptr, 1));
    }

    Capability GetRateFactor() const {
      return Capability_Attack(1);
    }

    void OnBegin(Object const& self, Data& data) {
      data = TaskPirateInstance();
    }

    void OnUpdate(Object const& self, float dt, Data& data) { AUTO_FRAME;
      TaskPirateInstance& it = data.Convert<TaskPirateInstance>();

      if (it.target && CanAttack(self, it.target))
        self->PushTask(Task_Destroy(it.target));

      /* TODO : Better target selection. */
      else {
        if (!it.target) {
          ComponentInterior* interior = args.zone->GetContainer()->GetInterior();
          it.target = interior->objects[RandI(interior->objects.size())];
        }
      }
    }
  };
}

DefineFunction(Task_Pirate) {
  return new TaskPirate(args);
}
