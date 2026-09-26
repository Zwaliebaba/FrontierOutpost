#include "SDFs.h"

#include "Bound.h"
#include "LteMath.h"

namespace {
  AutoClassDerived(SDFRadial, SDFT,
    SDF, source,
    float, rMin,
    float, rMax)
    DERIVED_TYPE_EX(SDFRadial)

    SDFRadial() {}

    float Evaluate(V3 const& p) const {
      return Length(p) - Mix(rMin, rMax, source->Evaluate(p));
    }

    Bound3 GetBound() const {
      return Bound3(V3(-rMax), V3(rMax));
    }

    void Encode(SDFProgram& program) const {
      source->Encode(program);
      program.Emit(SDFOp::Radial, {rMin, rMax});
    }
  };

  DERIVED_IMPLEMENT(SDFRadial)
}

DefineFunction(SDF_Radial) {
  return new SDFRadial(args.source, args.rMin, args.rMax);
}
