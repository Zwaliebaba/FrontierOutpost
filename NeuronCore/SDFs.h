#ifndef LTE_SDFs_h__
#define LTE_SDFs_h__

#include "DeclareFunction.h"
#include "SDF.h"

DeclareFunction(SDF_Cylinder, SDF,
  V3, center,
  V3, axis,
  float, radius)

DeclareFunction(SDF_FractalWorley, SDF,
  float, seed,
  int, octaves,
  float, lac)

DeclareFunction(SDF_Radial, SDF,
  SDF, source,
  float, rMin,
  float, rMax)

DeclareFunction(SDF_RoundBox, SDF,
  V3, center,
  V3, sides,
  float, radius)

DeclareFunction(SDF_Scale, SDF,
  SDF, source,
  V3, scale)

DeclareFunction(SDF_Shell, SDF,
  V3, center,
  float, radius,
  float, thickness)

DeclareFunction(SDF_Sphere, SDF,
  V3, center,
  float, radius)

DeclareFunction(SDF_Subtract, SDF,
  SDF, a,
  SDF, b,
  float, sharpness)

DeclareFunction(SDF_Torus, SDF,
  V3, center,
  float, radius,
  float, thickness)

#endif
