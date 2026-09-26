#ifndef LTE_SDF_h__
#define LTE_SDF_h__

#include "BaseType.h"
#include "Reference.h"
#include "LteString.h"
#include "V3.h"

#include <initializer_list>
#include <vector>

/* What an SDF becomes for the GPU (Design/ADR/ADR-009): postfix instructions that leave the
   field's value at a point, which Shaders/GenFieldCS.hlsl interprets for each voxel. A primitive
   pushes its value at the current point; Radial replaces the value on top with its own; a
   subtraction takes two values and pushes one; ScaleBegin saves the current point and divides it
   by the scale, and ScaleEnd restores it. The numbers are GenFieldCS.hlsl's OP_ macros. */
namespace SDFOp {
  enum Enum {
    Sphere = 1,
    RoundBox = 2,
    Cylinder = 3,
    Shell = 4,
    Torus = 5,
    FractalWorley = 6,
    Radial = 7,
    Subtract = 8,
    SubtractHard = 9,
    ScaleBegin = 10,
    ScaleEnd = 11
  };
}

/* An SDF's instructions, each its opcode and seven parameters, those it does not use 0, and how
   deep the interpreter's two stacks go for them. */
struct SDFProgram {
  std::vector<float> words;
  uint maxValues;
  uint maxPoints;

  SDFProgram() :
    maxValues(0),
    maxPoints(0),
    values(0),
    points(0)
    {}

  size_t Instructions() const {
    return words.size() / 8;
  }

  void Emit(SDFOp::Enum op, std::initializer_list<float> parameters = {}) {
    LTE_ASSERT(parameters.size() <= 7);
    words.push_back((float)op);
    words.insert(words.end(), parameters.begin(), parameters.end());
    words.resize(words.size() + (7 - parameters.size()), 0.0f);
    switch (op) {
    case SDFOp::Radial:
      break;
    case SDFOp::Subtract:
    case SDFOp::SubtractHard:
      values--;
      break;
    case SDFOp::ScaleBegin:
      points++;
      maxPoints = points > maxPoints ? points : maxPoints;
      break;
    case SDFOp::ScaleEnd:
      points--;
      break;
    default:
      values++;
      maxValues = values > maxValues ? values : maxValues;
      break;
    }
  }

private:
  uint values;
  uint points;
};

struct SDFT : public RefCounted {
  BASE_TYPE(SDFT)

  virtual float Evaluate(V3 const& p) const = 0;

  virtual Bound3 GetBound() const = 0;

  /* Appends the instructions that leave this SDF's value at the current point. */
  virtual void Encode(SDFProgram& program) const = 0;

  LT_API virtual V3 Gradient(V3 const& p) const;

  LT_API SDF Subtract(SDF const& other, float sharpness = -1);

  LT_API SDF Scale(V3 const& s);

  FIELDS {}
};

#endif
