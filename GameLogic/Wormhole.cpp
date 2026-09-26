#include "Objects.h"

#include "BoundingBox.h"
#include "Detectable.h"
#include "Drawable.h"
#include "Nameable.h"
#include "Navigable.h"
#include "Orientation.h"
#include "Zoned.h"

#include "Pool.h"
#include "Renderable.h"
#include "Script.h"
#include "SDFs.h"

#include "Icon.h"

#include "Visual.h"

namespace {
  /* The client's model, where there is a client (FrontierOutpost/WormholeVisual.cpp, ADR-016). */
  Renderable GetWormholeModel() {
    return (Renderable)Game::CreateVisual("Wormhole", nullptr);
  }
}

typedef ObjectWrapper
  < Component_BoundingBox
  < Component_Detectable
  < Component_Drawable
  < Component_Nameable
  < Component_Navigable
  < Component_Orientation
  < Component_Zoned
  < ObjectWrapperTail<ObjectType_Wormhole>
  > > > > > > > >
  WormholeBaseT;

AutoClassDerived(Wormhole, WormholeBaseT,
  Object, tunnel)

  DERIVED_TYPE_EX(Wormhole)
  POOLED_TYPE

  Wormhole() {
    Drawable.renderable = GetWormholeModel;
  }

  void Dock(Object const& docker) {
    Object const& dest = Navigable.nodes[0].dest;
    dest->GetContainer()->AddInterior(docker);
    docker->SetPos(
      dest->GetPos() - 1500.0f * Normalize(dest->GetPos()));
  }

  Icon GetIcon() const {
    Icon icon;
    ScriptFunction_Load("Icons:Wormhole")->Call(icon);
    return icon;
  }

  Signature GetSignature() const {
    return Signature(10.0f, 18.0f, 0.125f, 0.5f);
  }
};

DERIVED_IMPLEMENT(Wormhole)

DefineFunction(Object_Wormhole) {
  Reference<Wormhole> self = new Wormhole;
  self->Zoned.region = SDF_Sphere(0, 10);
  return self;
}

void Object_Wormholes(ObjectT* o1, ObjectT* o2) {
  Object a = Object_Wormhole();
  Object b = Object_Wormhole();

  a->GetNavigable()->nodes.push(NavigableNode(b, 0));
  b->GetNavigable()->nodes.push(NavigableNode(a, 0));

  a->SetName(o2->GetName() + " Wormhole");
  b->SetName(o1->GetName() + " Wormhole");

  V3 direction = Normalize(o2->GetPos() - o1->GetPos());
  a->SetPos( 400000.0f * direction);
  b->SetPos(-400000.0f * direction);
  a->SetScale(1000);
  b->SetScale(1000);

  o1->AddInterior(a);
  o2->AddInterior(b);
}
