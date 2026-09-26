#ifndef Graphics_Renderables_h__
#define Graphics_Renderables_h__

#include "DeclareFunction.h"
#include "Generic.h"

DeclareFunctionArgBind(Renderable_Asteroid, Renderable,
  uint, seed)

DeclareFunctionArgBind(Renderable_Starfield, Renderable,
  uint, seed,
  uint, starCount)

#endif
