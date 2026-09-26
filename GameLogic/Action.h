#ifndef Game_Action_h__
#define Game_Action_h__

#include "GameCommon.h"
#include "BaseType.h"
#include "Reference.h"

struct ActionT : public RefCounted {
  BASE_TYPE(ActionT)

  virtual void Execute(UpdateState&) const = 0;
  virtual String GetName() const = 0;
};

#endif
