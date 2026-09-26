#include "BeamImpl.h"

DERIVED_IMPLEMENT(BeamImpl)

Beam* Beam_Create() {
  return new BeamImpl;
}
