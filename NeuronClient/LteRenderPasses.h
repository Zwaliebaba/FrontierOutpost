#ifndef LTE_RenderPasses_h__
#define LTE_RenderPasses_h__

#include "DeclareFunction.h"
#include "RenderPass.h"
#include "LteString.h"

DeclareFunction(RenderPass_Bloom, RenderPass,
  int, radius,
  float, variance)

DeclareFunction(RenderPass_CustomFilter, RenderPass,
  Data, data)

DeclareFunction(RenderPass_PostFilter, RenderPass,
  String, shaderPath)

DeclareFunctionNoParams(RenderPass_Tonemap, RenderPass)

#endif
