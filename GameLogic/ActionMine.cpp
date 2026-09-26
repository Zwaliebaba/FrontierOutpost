#include "Actions.h"

#include "History.h"
#include "Mineable.h"

#include "Events.h"
#include "Object.h"

#include "LteMath.h"
#include "Pool.h"
#include "Transform.h"

namespace {
  AutoClassDerived(ActionMine, ActionT,
    Object, object,
    Object, target,
    Position, point)
    DERIVED_TYPE_EX(ActionMine)
    POOLED_TYPE

    ActionMine() {}

    void Execute(UpdateState& state) const {
      ObjectT* root = object->GetRoot();
      Pointer<ComponentMineable> mine = target->GetMineable();
      V3 phase = Normalize(target->GetTransform().InversePoint(point));

      /* TODO : Normalize in terms of mass. */
      float rate = Exp(-6.0f * (1.0f - Dot(phase, mine->phase)));
      rate *= state.dt * RandExp();
      rate *= (float)mine->quantity;
      rate *= 0.25f;

      Quantity quantity = Min(mine->quantity, (Quantity)rate);
      root->AddItem(mine->item, quantity);
      mine->quantity -= quantity;

      ObjectT* container = object->GetContainer();
      if (container && quantity > 0)
        Object_AddHistory(container,
          Event_Mined(object, target, mine->item, quantity));
    }

    String GetName() const {
      return "Mine";
    }
  };
}

DefineFunction(Action_Mine) {
  return new ActionMine(args.object, args.target, args.point);
}
