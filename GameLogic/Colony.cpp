#include "Colony.h"

DERIVED_IMPLEMENT(Colony)

DefineFunction(Object_Colony) {
  Reference<Colony> self = new Colony(args.planet, args.population);
  self->Seeded.seed = args.seed;
  self->traits = args.type->GetTraits();
  self->SetSupertype(args.type);
  self->Initialize();
  self->PushTask(args.type->GetTask());
  ScriptFunction_Load("Object/Colony:Init")->VoidCall(0, DataRef((Object)self));
  return self;
}
