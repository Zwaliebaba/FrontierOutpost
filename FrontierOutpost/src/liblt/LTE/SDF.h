#ifndef LTE_SDF_h__
#define LTE_SDF_h__

#include "BaseType.h"
#include "Reference.h"
#include "String.h"
#include "V3.h"

struct SDFT : public RefCounted {
  BASE_TYPE(SDFT)

  virtual float Evaluate(V3 const& p) const = 0;

  virtual Bound3 GetBound() const = 0;

  virtual String GetCode() const {
    return GetCode("p");
  }

  virtual String GetCode(String const& p) const {
    return "0";
  }

  LT_API virtual V3 Gradient(V3 const& p) const;

  LT_API SDF Subtract(SDF const& other, float sharpness = -1);

  LT_API SDF Scale(V3 const& s);

  FIELDS {}
};

#endif
