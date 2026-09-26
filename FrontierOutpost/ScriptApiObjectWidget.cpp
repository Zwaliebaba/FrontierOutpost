#include "GameCommon.h"
#include "Object.h"
#include "Player.h"

#include "Function.h"

#include "Widget.h"

/* Object_GetWidget, which was ScriptApiObject.cpp's: a widget is the client's, so the client holds
   it, and the object writes it through ObjectT::GetWidget (ADR-016). Scripts call it as before. */

FreeFunction(Widget, Object_GetWidget,
  "Return the object-specific widget for 'object' from 'player's point-of-view",
  Object, object,
  Player, player)
{
  Widget widget;
  object->GetWidget(player, &widget);
  return widget;
} FunctionAlias(Object_GetWidget, GetWidget);
