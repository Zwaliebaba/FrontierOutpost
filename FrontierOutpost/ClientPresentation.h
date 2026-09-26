#ifndef ClientPresentation_h__
#define ClientPresentation_h__

#include "LteCommon.h"

struct ComponentInterior;

/* The particles an interior keeps, which the client's presentation owns (ADR-016). Made on first
   use. */
ParticleSystem const& ClientPresentation_Particles(ComponentInterior const* interior);

#endif
