#include "Trail.h"

DERIVED_IMPLEMENT(Trail)

Object Object_Trail(
  Object const& parent,
  int length,
  Color const& color,
  float size)
{
  Reference<Trail> self = new Trail;
  self->length = length;
  self->color = color;
  self->size = size;

  /* TODO : Detach upon parent deletion so trail persists. */
  parent->AddChild(self);
  return self;
}
