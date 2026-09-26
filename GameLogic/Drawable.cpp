#include "Drawable.h"
#include "Cullable.h"

#include "Object.h"

#include "DrawState.h"
#include "Function.h"
#include "RenderStyle.h"

void ComponentDrawable::Draw(ObjectT* self, DrawState* state) {
  if (!renderable)
    return;

  RenderStyle_Get()->SetTransform(self->GetTransform());
  // DrawState_Push("objectRadius", self->GetRadius());
  renderable()->Render(state);
  // DrawState_Pop("objectRadius");
}

VoidFreeFunction(Object_SetRenderable,
  "Set 'object's renderable to 'renderable'",
  Object, object,
  Renderable, renderable)
{
  object->GetDrawable()->renderable = renderable;
} FunctionAlias(Object_SetRenderable, SetRenderable);
