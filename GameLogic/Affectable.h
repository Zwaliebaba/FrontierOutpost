#ifndef Component_Affectable_h__
#define Component_Affectable_h__

#include "ComponentCommon.h"
#include "UpdateState.h"
#include "AutoClass.h"
#include "AutoPtr.h"
#include "BaseType.h"
#include "Vector.h"

struct Affector {
  BASE_TYPE(Affector)
  float life;
  bool deleted;

  Affector() :
    life(1),
    deleted(false)
    {}

  void Run(ObjectT* self, float dt) {
    life -= dt;
    if (life <= 0)
      deleted = true;
    else
      OnRun(self, dt);
  }

  virtual void OnRun(ObjectT* self, float dt) = 0;

  FIELDS {
    MAPFIELD(life)
  }
};

AutoClass(ComponentAffectable,
  Vector<AutoPtr<Affector> >, affectors)

  ComponentAffectable() {}

  void Add(Affector* a) {
    affectors << a;
  }

  void Run(ObjectT* self, UpdateState& state) {
    for (int i = 0; i < (int)affectors.size(); ++i) {
      affectors[i]->Run(self, state.dt);
      if (affectors[i]->deleted) {
        affectors.removeIndex(i);
        i--;
        continue;
      }
    }
  }
};

AutoComponent(Affectable)
  void OnUpdate(UpdateState& s) {
    Affectable.Run(this, s);
    BaseT::OnUpdate(s);
  }

  void AddAffector(Affector* a) {
    Affectable.Add(a);
  }
};

#endif
