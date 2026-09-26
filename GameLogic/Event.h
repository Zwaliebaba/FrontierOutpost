#ifndef Game_Event_h__
#define Game_Event_h__

#include "GameCommon.h"
#include "BaseType.h"
#include "Reference.h"

struct EventT : public RefCounted {
  BASE_TYPE(EventT)

  FIELDS {}
};

#endif
