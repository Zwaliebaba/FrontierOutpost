#ifndef Game_Order_h__
#define Game_Order_h__

#include "GameCommon.h"

#include "AutoClass.h"
#include "DeclareFunction.h"
#include "Pool.h"
#include "Reference.h"

AutoClassDerived(OrderT, RefCounted,
  Object, owner,
  Item, item,
  Quantity, volume,
  Quantity, price,
  Quantity, filledVolume,
  Quantity, filledTotal,
  Object, node)
  POOLED_TYPE

  OrderT() :
    volume(0),
    price(0),
    filledVolume(0),
    filledTotal(0)
    {}
};

DeclareFunction(Order_Create, Order,
  Object, owner,
  Item, item,
  Quantity, volume,
  Quantity, price)

#endif
