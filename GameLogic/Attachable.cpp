#include "Attachable.h"
#include "Orientation.h"

#include "Object.h"

#include "Matrix.h"

void ComponentAttachable::UpdateTransform(ObjectT* self) {
  if (self->parent) {
    Pointer<ComponentOrientation> myOrientation = self->GetOrientation();
    myOrientation->transform = self->parent->GetTransform() * transform;
    // myOrientation->transform.scale = transform.scale;
    myOrientation->version++;

    if (moved) {
      moved = false;
      transform.Orthogonalize();
    }
  }
}
