#include "TransferUnit.h"

DERIVED_IMPLEMENT(TransferUnit)

DefineFunction(Object_TransferUnit) {
  Reference<TransferUnit> self = new TransferUnit();
  self->SetSupertype(args.type);
  /* Drawn by the client's visual, where there is a client (ADR-016). */
  self->Drawable.renderable = (Renderable)Game::CreateVisual("TransferUnit", self);
  return self;
}
