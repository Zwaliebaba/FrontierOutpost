#include "Thruster.h"

DERIVED_IMPLEMENT(Thruster)

Object Object_Thruster(Item const& type, ObjectT* parent) {
  Reference<Thruster> self = new Thruster;
  self->SetSupertype(type);
  /* Drawn by the client's visual, where there is a client (ADR-016). */
  Renderable visual;
  Game::CreateVisual("Thruster", self, visual);
  self->Drawable.renderable = visual;
  ScriptFunction_Load("Object/Thruster:Init")->VoidCall(0, DataRef((Object)self));
  return self;
}
