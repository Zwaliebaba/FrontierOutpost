#include "Drawable.h"
#include "Cullable.h"

#include "Object.h"

#include "Function.h"

#include "Presentation.h"

void ComponentDrawable::Draw(ObjectT* self, DrawState* state) {
  if (!renderable)
    return;

  /* Drawn by the client's presentation, where there is one (ADR-016). */
  if (Game::Presentation* presentation = Game::GetPresentation())
    presentation->DrawObject(self, renderable().t, state);
}

VoidFreeFunction(Object_SetRenderable,
  "Set 'object's renderable to 'renderable'",
  Object, object,
  Renderable, renderable)
{
  object->GetDrawable()->renderable = renderable;
} FunctionAlias(Object_SetRenderable, SetRenderable);
