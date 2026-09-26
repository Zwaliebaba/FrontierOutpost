#include "Objects.h"

#include "Visual.h"

typedef ObjectWrapper
  < ObjectWrapperTail<ObjectType_Effect>
  >
  DustFlecksBaseT;

AutoClassDerived(DustFlecks, DustFlecksBaseT,
  Vector<Object>, elements)

  DERIVED_TYPE_EX(DustFlecks)

  DustFlecks() {}

  /* Drawn by the client, where there is one (FrontierOutpost/DustFlecksVisual.cpp, ADR-016). */
  void OnDraw(DrawState* state) {
    Game::Draw("DustFlecks", Game::DrawPhase::Object, this, state);
  }
};

DERIVED_IMPLEMENT(DustFlecks)

DefineFunction(Object_DustFlecks) {
  return new DustFlecks();
}
