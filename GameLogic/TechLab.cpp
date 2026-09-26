#include "Objects.h"

#include "Orientation.h"
#include "Pluggable.h"
#include "Supertyped.h"
#include "ComponentTasks.h"

#include "Player.h"
#include "AssemblyChip.h"

#include "Pool.h"

#include "SoundEngine.h"

typedef ObjectWrapper
  < Component_Orientation
  < Component_Pluggable
  < Component_Supertyped
  < Component_Tasks
  < ObjectWrapperTail<ObjectType_TechLab>
  > > > > >
  TechLabBaseT;

AutoClassDerivedEmpty(TechLab, TechLabBaseT)
  Sound sound;

  DERIVED_TYPE_EX(TechLab)
  POOLED_TYPE
  
  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);

    if (!sound)
      sound = Sound_Play3D("techlab/loop.wav",
        GetRoot().t, 0, 0,
        0.1f * GetRoot()->GetScale().GetMax(), true);
    sound->SetVolume(GetCurrentTask() == nullptr ? 0.0f : 0.1f);
  }
};

DERIVED_IMPLEMENT(TechLab)

DefineFunction(Object_TechLab) {
  Reference<TechLab> self = new TechLab;
  self->SetSupertype(args.type);
  return self;
}
