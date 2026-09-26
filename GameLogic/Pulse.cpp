#include "Pulse.h"

DERIVED_IMPLEMENT(Pulse)

Object Object_Pulse(
  V3 const& velocity,
  V3 const& drift,
  float width)
{
  Reference<Pulse> self = new Pulse;
  self->SetLook(Normalize(velocity));
  self->speed = Length(velocity);
  self->drift = drift;
  self->width = kSizeMult * width;
  self->length = kLengthMult * self->width;
  return self;
}
