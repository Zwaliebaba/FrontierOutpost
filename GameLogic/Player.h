#ifndef Game_Player_h__
#define Game_Player_h__

#include "Object.h"
#include "ObjectWrapper.h"
#include "Stats.h"

#include "AttributeTraits.h"

#include "Account.h"
#include "Affectable.h"
#include "Assets.h"
#include "Info.h"
#include "Log.h"
#include "Missions.h"
#include "Nameable.h"
#include "Opinion.h"
#include "Orders.h"
#include "Projects.h"
#include "Scriptable.h"
#include "ComponentTasks.h"

#include "DeclareFunction.h"

typedef ObjectWrapper
  < Attribute_Traits
  < Component_Account
  < Component_Affectable
  < Component_Assets
  < Component_Info
  < Component_Log
  < Component_Missions
  < Component_Nameable
  < Component_Opinions
  < Component_Orders
  < Component_Projects
  < Component_Scriptable
  < Component_Tasks
  < ObjectWrapperTail<ObjectType_Player>
  > > > > > > > > > > > > > >
  PlayerBaseT;

struct PlayerT : public PlayerBaseT {
  BASE_TYPE_EX(PlayerT)

  Object piloting;
  Stats stats;

  virtual bool IsHuman() const = 0;

  virtual void OnAttacked(Player const&) = 0;

  virtual void Pilot(Object const&) = 0;

  virtual void Unpilot() = 0;
};

DeclareFunction(Player_AI, Player,
  Traits, traits)

DeclareFunctionNoParams(Player_Human, Player)

#endif
