#ifndef System_h__
#define System_h__

#include "Objects.h"

#include "Economy.h"
#include "History.h"
#include "Interior.h"
#include "Nameable.h"
#include "Orientation.h"
#include "Queryable.h"
#include "ComponentResources.h"
#include "Seeded.h"

#include "Renderable.h"

#include "Visual.h"

typedef ObjectWrapper
  < Attribute_Traits
  < Component_Economy
  < Component_History
  < Component_Interior
  < Component_Nameable
  < Component_Orientation
  < Component_Queryable
  < Component_Resources
  < Component_Seeded
  < ObjectWrapperTail<ObjectType_System>
  > > > > > > > > > >
  SystemBaseT;

AutoClassDerived(System, SystemBaseT,
  Object, star)
  DERIVED_TYPE_EX(System)
  POOLED_TYPE

  /* What the system looks like: its nebula, its starfield and its colour curves, which the client
     makes from the system's seed (FrontierOutpost/SystemVisual.cpp, ADR-016). Null without one. */
  Renderable visual;

  System() {}

  void Initialize() {
    Interior.allowMovement = true;
    Game::CreateVisual("System", this, visual);
  }

  void BeginDrawInterior(DrawState* state) {
    Game::Draw("System", Game::DrawPhase::BeginInterior, this, state);
  }

  void OnDrawInterior(DrawState* state) {
    Game::Draw("System", Game::DrawPhase::Interior, this, state);
  }

  void EndDrawInterior(DrawState* state) {
    Game::Draw("System", Game::DrawPhase::EndInterior, this, state);
  }
};

#endif
