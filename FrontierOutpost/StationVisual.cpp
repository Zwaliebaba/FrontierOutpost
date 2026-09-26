#include "Visual.h"

#include "Object.h"
#include "Supertyped.h"
#include "StationType.h"

#include "Model.h"
#include "RenderStyle.h"
#include "Transform.h"

/* How the inside of a Station is drawn: the client's half of Station.cpp (ADR-016). */

namespace {
  void OnDrawInterior(ObjectT* self, DrawState* state) {
    RenderStyle_Get()->SetTransform(Transform_Scale(2000));
    ((StationType*)(ItemT*)self->GetSupertyped()->type)->interiorModel->Render(state);
  }

  bool registered = (Game::RegisterDraw("Station", Game::DrawPhase::Interior, OnDrawInterior), true);
}
