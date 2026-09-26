#include "Objects.h"

#include "BoundingBox.h"
#include "Drawable.h"
#include "Motion.h"
#include "Orientation.h"

#include "Pool.h"

typedef ObjectWrapper
  < Component_BoundingBox
  < Component_Drawable
  < Component_Motion
  < Component_Orientation
  < ObjectWrapperTail<ObjectType_Dynamic>
  > > > > >
  DynamicBaseT;

AutoClassDerivedEmpty(Dynamic, DynamicBaseT)
  DERIVED_TYPE_EX(Dynamic)
  POOLED_TYPE
};

DERIVED_IMPLEMENT(Dynamic)

Object Object_Dynamic(Generic<Renderable> const& renderable) {
  Reference<Dynamic> self = new Dynamic;
  if (renderable)
    self->Drawable.renderable = renderable;
  return self;
}
