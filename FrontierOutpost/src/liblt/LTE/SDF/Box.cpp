#include "../SDFs.h"

#include "LTE/Bound.h"
#include "LTE/Math.h"
#include "LTE/TypeTraits.h"

namespace {
  AutoClassDerived(SDFRoundBox, SDFT,
    V3, center,
    V3, sides,
    float, radius)

    DERIVED_TYPE_EX(SDFRoundBox)

    SDFRoundBox() :
      center(0),
      sides(1),
      radius(.5f)
      {}

    float Evaluate(V3 const& p) const {
      return Length(Max(Abs(p - center) - sides * (1.f - radius), V3(0))) - radius;
    }

    Bound3 GetBound() const {
      return Bound3(center - sides, center + sides);
    }

    String GetCode(String const& p) const {
      return Stringize()
        | "boxr(" | p | ", " | center | ", " | sides | ", "
        | radius | ")";
    }
  };

  DERIVED_IMPLEMENT(SDFRoundBox)
}

DefineFunction(SDF_RoundBox) {
  return new SDFRoundBox(args.center, args.sides, args.radius);
}
