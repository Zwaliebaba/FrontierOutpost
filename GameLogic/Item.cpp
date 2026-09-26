#include "Item.h"

#include "Traits.h"
#include "Capability.h"
#include "Object.h"
#include "Task.h"
#include "Data.h"
#include "Renderable.h"
#include "Static.h"
#include "Icon.h"

ItemT::ItemT() {
  Data& nextItemID = Static_Get("nextItemID");
  if (!nextItemID)
    nextItemID = ItemID(0);
  id = nextItemID.Convert<ItemID>()++;
}

Object ItemT::Instantiate(ObjectT* parent) {
  return nullptr;
}

bool ItemT::IsType(Item const& type) const {
  if (this == type.t)
    return true;
  ItemT* super = GetSuperType();
  return super ? super->IsType(type) : false;
}

#define X(type, name, default)                                                 \
  type const& ItemT::Get##name() const {                                       \
    static type value;                                                         \
    value = default;                                                           \
    return value;                                                              \
  }
ITEMPROPERTY_X
#undef X
