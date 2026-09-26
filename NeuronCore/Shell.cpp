#include "SDFs.h"

#include "Bound.h"
#include "LteMath.h"

namespace {
  AutoClassDerived(SDFShell, SDFT,
    V3, center,
    float, radius,
    float, thickness)
    DERIVED_TYPE_EX(SDFShell)

    SDFShell() {}

    float Evaluate(V3 const& p) const {
      return Abs(Length(p - center) - radius) - thickness;
    }

    Bound3 GetBound() const {
      return Bound3(center - V3(radius + thickness),
                  center + V3(radius + thickness));
    }

    void Encode(SDFProgram& program) const {
      program.Emit(SDFOp::Shell, {center.x, center.y, center.z, radius, thickness});
    }
  };

  DERIVED_IMPLEMENT(SDFShell)
}

DefineFunction(SDF_Shell) {
  return new SDFShell(args.center, args.radius, args.thickness);
}
