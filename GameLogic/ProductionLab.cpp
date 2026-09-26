#include "Objects.h"

#include "Orientation.h"
#include "Pluggable.h"
#include "Supertyped.h"
#include "ComponentTasks.h"

#include "Pool.h"

#include "SoundEngine.h"

typedef ObjectWrapper
  < Component_Orientation
  < Component_Pluggable
  < Component_Supertyped
  < Component_Tasks
  < ObjectWrapperTail<ObjectType_ProductionLab>
  > > > > >
  ProductionLabBaseT;

AutoClassDerivedEmpty(ProductionLab, ProductionLabBaseT)
  Sound sound;

  DERIVED_TYPE_EX(ProductionLab)
  POOLED_TYPE

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);

    if (!sound)
      sound = Sound_Play3D("productionlab/loop.wav", GetRoot().t, 0, 0, 1, true);
    sound->SetVolume(GetCurrentTask() == nullptr ? 0.0f : 0.1f);
  }
};

DERIVED_IMPLEMENT(ProductionLab)

DefineFunction(Object_ProductionLab) {
  Reference<ProductionLab> self = new ProductionLab;
  self->SetSupertype(args.type);
  return self;
}
