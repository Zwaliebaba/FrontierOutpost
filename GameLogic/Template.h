#ifndef Game_Template_h__
#define Game_Template_h__

#include "GameCommon.h"
#include "Item.h"
#include "AutoClass.h"

AutoClassDerived(TemplateT, RefCounted,
  String, name,
  Item, item,
  Vector<Item>, hardpoints,
  Vector<ItemQuantity>, cargo)

  TemplateT() {}
};

#endif
