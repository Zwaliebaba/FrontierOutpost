#include "ItemProperty.h"

#include "Capability.h"
#include "Object.h"
#include "Task.h"

#include "Renderable.h"

#include "Icon.h"

#define X(type, name, default)                                                 \
  DERIVED_IMPLEMENT(ItemProperty_##name##T)                                    \
  DataRef ItemProperty_##name##T::Evaluate(Item const& item) const {           \
    return item->Get##name();                                                  \
  }
ITEMPROPERTY_X
#undef X
