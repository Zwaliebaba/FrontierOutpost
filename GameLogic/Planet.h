#ifndef Planet_h__
#define Planet_h__

#include "ObjectWrapper.h"

#include "BoundingBox.h"
#include "Collidable.h"
#include "Cullable.h"
#include "Detectable.h"
#include "Drawable.h"
#include "Nameable.h"
#include "Orientation.h"
#include "ComponentResources.h"
#include "Seeded.h"
#include "Supertyped.h"

#include "Order.h"
#include "Player.h"

typedef ObjectWrapper
  < Component_BoundingBox
  < Component_Collidable
  < Component_Cullable
  < Component_Detectable
  < Component_Drawable
  < Component_Nameable
  < Component_Orientation
  < Component_Resources
  < Component_Seeded
  < Component_Supertyped
  < ObjectWrapperTail<ObjectType_Planet>
  > > > > > > > > > > >
  PlanetBaseT;

struct Planet : public PlanetBaseT {
  Object orbitalStation;
  Player manager;

  Color surfaceColor1;
  Color surfaceColor2;
  Color atmoTint;
  float cloudLevel;
  float atmoDensity;
  float heightMult;
  float oceanLevel;
};

#endif
