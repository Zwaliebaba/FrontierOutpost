#ifndef UI_Compositors_h__
#define UI_Compositors_h__

#include "UiCommon.h"
#include "Compositor.h"
#include "DeclareFunction.h"

DeclareFunctionNoParams(Compositor_None, Compositor)

DeclareFunction(Compositor_Basic, Compositor,
  float, noise,
  float, lines,
  V3, gradeBlue)

#endif
