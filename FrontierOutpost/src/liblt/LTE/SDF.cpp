#include "SDFs.h"
#include "Bound.h"
#include "Math.h"
#include "Plane.h"
#include "Vector.h"

namespace {
  AutoClassDerived(FractalWorley, SDFT,
    float, seed,
    int, octaves,
    float, lac)
    DERIVED_TYPE_EX(FractalWorley)

    FractalWorley() {}

    float Evaluate(V3 const& p) const {
      NOT_IMPLEMENTED
      return 1;
    }

    Bound3 GetBound() const {
      return Bound3(-FLT_MAX, FLT_MAX);
    }

    void Encode(SDFProgram& program) const {
      program.Emit(SDFOp::FractalWorley, {seed, (float)octaves, lac});
    }
  };

  DERIVED_IMPLEMENT(FractalWorley)
}

DefineFunction(SDF_FractalWorley) {
  return new FractalWorley(args.seed, args.octaves, args.lac);
}

V3 SDFT::Gradient(V3 const& point) const {
  const float ds = 1e-5f;
  return V3(
    Evaluate(point + V3(ds, 0, 0)) - Evaluate(point - V3(ds, 0, 0)),
    Evaluate(point + V3(0, ds, 0)) - Evaluate(point - V3(0, ds, 0)),
    Evaluate(point + V3(0, 0, ds)) - Evaluate(point - V3(0, 0, ds))) / V3(ds);
}

SDF SDFT::Scale(V3 const& s) {
  return SDF_Scale(this, s);
}
