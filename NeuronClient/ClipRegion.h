#ifndef UI_ClipRegion_h__
#define UI_ClipRegion_h__

#include "UiCommon.h"
#include "DeclareFunction.h"
#include "V2.h"

DeclareFunctionNoParams(ClipRegion_GetMin, V2)
DeclareFunctionNoParams(ClipRegion_GetMax, V2)

DeclareFunctionNoParams(ClipRegion_Pop, void)

DeclareFunction(ClipRegion_Push, void,
  V2, pos,
  V2, size)

DeclareFunctionNoParams(ClipRegion_PushNoClip, void)

#endif
