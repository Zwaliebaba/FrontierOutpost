#include "Rail.h"

DERIVED_IMPLEMENT(Rail)

Object Object_Rail(
  Position const& origin,
  V3 const& direction,
  V3 const& velocity)
{
  Reference<Rail> self = new Rail;
  self->position = origin;
  self->direction = direction;
  self->velocity = velocity;
  return self;
}
