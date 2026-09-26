#include "Explosion.h"

DERIVED_IMPLEMENT(Explosion)

DefineFunction(Object_Explosion) {
  Reference<Explosion> self = new Explosion;
  self->type = args.type;
  self->age = args.age;
  self->duration = args.duration;
  return self;
}
