#include "SDFs.h"

#include "Bound.h"
#include "LteMath.h"

namespace {
  AutoClassDerived(SDFSphere, SDFT,
    V3, center,
    float, radius)
    DERIVED_TYPE_EX(SDFSphere)

    SDFSphere() {}

    float Evaluate(V3 const& p) const {
      return Length(p - center) - radius;
    }

    Bound3 GetBound() const {
      return Bound3(center - V3(radius), center + V3(radius));
    }

    void Encode(SDFProgram& program) const {
      program.Emit(SDFOp::Sphere, {center.x, center.y, center.z, radius});
    }
  };

  DERIVED_IMPLEMENT(SDFSphere)
}

DefineFunction(SDF_Sphere) {
  return new SDFSphere(args.center, args.radius);
}
