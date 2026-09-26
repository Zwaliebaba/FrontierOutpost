#ifndef RenderPasses_h__
#define RenderPasses_h__

#include "GameCommon.h"
#include "DeclareFunction.h"
#include "RenderPass.h"
#include "V3.h"
#include "V4.h"

DeclareFunctionNoParams(RenderPass_Blended, RenderPass)

DeclareFunction(RenderPass_Camera, RenderPass,
  Camera, camera)

DeclareFunction(RenderPass_Clear, RenderPass,
  V4, value)

DeclareFunctionNoParams(RenderPass_DepthPrepass, RenderPass)

DeclareFunctionNoParams(RenderPass_GBuffer, RenderPass)

DeclareFunctionNoParams(RenderPass_GlobalLighting, RenderPass)

DeclareFunctionNoParams(RenderPass_LocalLighting, RenderPass)

DeclareFunctionNoParams(RenderPass_LensFlares, RenderPass)

DeclareFunctionNoParams(RenderPass_Particles, RenderPass)

DeclareFunctionNoParams(RenderPass_SMAA, RenderPass)

DeclareFunctionNoParams(RenderPass_Visibility, RenderPass)

#endif
