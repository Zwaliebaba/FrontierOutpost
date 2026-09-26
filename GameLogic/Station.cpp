#include "Objects.h"

#include "Affectable.h"
#include "Asset.h"
#include "BoundingBox.h"
#include "Cargo.h"
#include "Collidable.h"
#include "Crew.h"
#include "Cullable.h"
#include "Detectable.h"
#include "Dockable.h"
#include "Drawable.h"
#include "Explodable.h"
#include "Integrity.h"
#include "Interior.h"
#include "Market.h"
#include "MissionBoard.h"
#include "Nameable.h"
#include "Orientation.h"
#include "Pilotable.h"
#include "Sockets.h"
#include "Storage.h"
#include "Supertyped.h"
#include "Targets.h"
#include "Zoned.h"

#include "StationType.h"

#include "DrawState.h"
#include "Grammar.h"
#include "Model.h"
#include "Pointer.h"
#include "Pool.h"
#include "RNG.h"
#include "RenderStyle.h"
#include "SDFs.h"

typedef ObjectWrapper
  < Component_Affectable
  < Component_Asset
  < Component_BoundingBox
  < Component_Cargo
  < Component_Collidable
  < Component_Crew
  < Component_Cullable
  < Component_Detectable
  < Component_Dockable
  < Component_Drawable
  < Component_Explodable
  < Component_Integrity
  < Component_Interior
  < Component_Market
  < Component_MissionBoard
  < Component_Nameable
  < Component_Orientation
  < Component_Pilotable
  < Component_Sockets
  < Component_Storage
  < Component_Supertyped
  < Component_Targets
  < Component_Zoned
  < ObjectWrapperTail<ObjectType_Station>
  > > > > > > > > > > > > > > > > > > > > > > > >
  StationBaseT;

AutoClassDerivedEmpty(Station, StationBaseT)
  DERIVED_TYPE_EX(Station)
  POOLED_TYPE

  void Initialize() {
    Interior.allowMovement = false;
    Zoned.region = SDF_Sphere(0, Length(0.5f * GetLocalBound().GetSideLengths()));
  }

  void BeginDrawInterior(DrawState* state) {
    GetContainer()->BeginDrawInterior(state);
  }

  void OnDrawInterior(DrawState* state) {
    RenderStyle_Get()->SetTransform(Transform_Scale(2000));
    ((StationType*)(ItemT*)Supertyped.type)->interiorModel->Render(state);
  }

  void EndDrawInterior(DrawState* state) {
    GetContainer()->EndDrawInterior(state);
  }
};

DERIVED_IMPLEMENT(Station)

DefineFunction(Object_Station) {
  Reference<Station> self = new Station;
  self->SetSupertype(args.type);
  self->Initialize();
  return self;
}
