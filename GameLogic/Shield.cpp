#include "Shield.h"

VectorMap<size_t, Mesh>& Shield_MeshCache() {
  /* CRITICAL : Seriously..? That's going to cause problems. */
  static VectorMap<size_t, Mesh> meshCache;
  return meshCache;
}

DERIVED_IMPLEMENT(Shield)

DefineFunction(Object_Shield) {
  LTE_ASSERT(args.type->GetType() == ItemType_ShieldType);
  Reference<Shield> self = new Shield;
  self->SetSupertype(args.type);
  return self;
}
