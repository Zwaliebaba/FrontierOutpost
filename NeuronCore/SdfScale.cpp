#include "SDFs.h"

#include "Bound.h"
#include "LteMath.h"

namespace {
  AutoClassDerived(SDFScale, SDFT,
    SDF, source,
    V3, scale)
    DERIVED_TYPE_EX(SDFScale)

    SDFScale() {}

    float Evaluate(V3 const& p) const {
      return source->Evaluate(p / scale);
    }

    Bound3 GetBound() const {
      Bound3 bound = source->GetBound();
      return Bound3(bound.lower * scale, bound.upper * scale);
    }

    void Encode(SDFProgram& program) const {
      program.Emit(SDFOp::ScaleBegin, {scale.x, scale.y, scale.z});
      source->Encode(program);
      program.Emit(SDFOp::ScaleEnd);
    }
  };

  DERIVED_IMPLEMENT(SDFScale)
}

DefineFunction(SDF_Scale) {
  return new SDFScale(args.source, args.scale);
} FunctionAlias(SDF_Scale, *);
