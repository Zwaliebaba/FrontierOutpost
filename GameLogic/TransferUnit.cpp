#include "TransferUnit.h"

DERIVED_IMPLEMENT(TransferUnit)

DefineFunction(Object_TransferUnit) {
  Reference<TransferUnit> self = new TransferUnit();
  self->SetSupertype(args.type);
  /* Drawn by the client's visual, where there is a client (ADR-016). */
  Renderable visual;
  Game::CreateVisual("TransferUnit", self, visual);
  self->Drawable.renderable = visual;
  return self;
}
