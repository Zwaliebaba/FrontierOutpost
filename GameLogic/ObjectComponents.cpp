#include "Account.h"
#include "Affectable.h"
#include "Asset.h"
#include "Assets.h"
#include "Attachable.h"
#include "BoundingBox.h"
#include "Cargo.h"
#include "Collidable.h"
#include "Crew.h"
#include "Cullable.h"
#include "Damager.h"
#include "Database.h"
#include "Detectable.h"
#include "Dockable.h"
#include "Drawable.h"
#include "Economy.h"
#include "Explodable.h"
#include "History.h"
#include "Integrity.h"
#include "Interior.h"
#include "Log.h"
#include "Market.h"
#include "Mineable.h"
#include "MissionBoard.h"
#include "Missions.h"
#include "Motion.h"
#include "MotionControl.h"
#include "Nameable.h"
#include "Navigable.h"
#include "Orders.h"
#include "Orientation.h"
#include "Pilotable.h"
#include "Pluggable.h"
#include "Projects.h"
#include "ProximityTracker.h"
#include "Queryable.h"
#include "ComponentResources.h"
#include "Scriptable.h"
#include "Sockets.h"
#include "Storage.h"
#include "Supertyped.h"
#include "Targets.h"
#include "ComponentTasks.h"
#include "Zoned.h"

#include "Function.h"

#if 1
#define X(x)                                                                   \
  FreeFunction(Component##x*, Object_GetComponent##x,                          \
    "Return the " #x " component of 'object'",                                 \
    Object, object)                                                            \
  {                                                                            \
    return object->Get##x();                                                   \
  } FunctionAlias(Object_GetComponent##x, GetComponent##x);                    \
                                                                               \
  FreeFunction(bool, Object_HasComponent##x,                                   \
    "Return whether 'object' has a " #x " component",                          \
    Object, object)                                                            \
  {                                                                            \
    return object->Get##x() != nullptr;                                        \
  } FunctionAlias(Object_HasComponent##x, HasComponent##x);
#define Y(x, y) X(x)
COMPONENT_X
#undef X
#undef Y
#endif
