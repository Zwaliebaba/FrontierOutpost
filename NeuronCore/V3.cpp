#include "V3.h"
#include "LteMath.h"

V3 RandV3(float min, float max) {
  float x = Rand(min, max);
  float y = Rand(min, max);
  float z = Rand(min, max);
  return V3(x, y, z);
}
