#ifndef Universe_h__
#define Universe_h__

#include "Object.h"
#include "ObjectWrapper.h"
#include "Item.h"

#include "Interior.h"
#include "Nameable.h"
#include "ComponentResources.h"
#include "Seeded.h"

#include "Distribution.h"

typedef ObjectWrapper
  < Component_Interior
  < Component_Nameable
  < Component_Resources
  < Component_Seeded
  < ObjectWrapperTail<ObjectType_Universe>
  > > > > >
  UniverseBaseT;

struct Universe : public UniverseBaseT {
  uint depth;
  Time age;
  Distribution<Item> colonyTypes;
  Vector<Object> loadedRegions;
  Item currencyBasis;

  Universe() :
    age(0)
    {}
};

LT_API Universe* Universe_Get();

#endif
