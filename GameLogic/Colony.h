#ifndef Colony_h__
#define Colony_h__

#include "Objects.h"

#include "Account.h"
#include "Asset.h"
#include "Attachable.h"
#include "BoundingBox.h"
#include "Cargo.h"
#include "Dockable.h"
#include "Drawable.h"
#include "Interior.h"
#include "Market.h"
#include "MissionBoard.h"
#include "Nameable.h"
#include "Orientation.h"
#include "Scriptable.h"
#include "Storage.h"
#include "Supertyped.h"
#include "ComponentTasks.h"
#include "Zoned.h"

#include "GameTasks.h"
#include "AttributeTraits.h"
#include "Planet.h"

#include "LteMath.h"
#include "RNG.h"
#include "Renderable.h"
#include "Script.h"
#include "SDFs.h"

#include "Widget.h"

#include "Visual.h"

const uint kTerrainQuality = 750;
const float kTerrainScale = 50000;
const float kTrees = 100;

/* The colony's terrain: the game places the hangar on it, and the client draws it
   (FrontierOutpost/ColonyVisual.cpp, ADR-016). */
inline float RidgedWorley(V2 const& uv) {
  return Abs(2.0f * WorleyNoise2D(uv) - 1.0f);
}

inline float TerrainFn(V2 const& uv) {
  float h = Abs(Fractal(RidgedWorley, uv * 3.0f, 10, 2.2f) - 0.5f);
  h = Min(h, Fractal(WorleyNoise2D, uv * 2.0f + 13.0f, 10, 2.0f));
  h *= h;
  return h - 0.01f;
}

inline float HeightFn(V2 const& uv) {
  return TerrainFn(uv) * 0.03f;
}

inline V3 SpatialFn(V2 const& uv) {
  return kTerrainScale * Normalize(V3(uv.x, 5.0f, uv.y)) * (1.0f + HeightFn(uv));
}

typedef ObjectWrapper
  < Attribute_Traits
  < Component_Account
  < Component_Asset
  < Component_Attachable
  < Component_BoundingBox
  < Component_Cargo
  < Component_Dockable
  < Component_Drawable
  < Component_Interior
  < Component_Market
  < Component_MissionBoard
  < Component_Nameable
  < Component_Orientation
  < Component_Scriptable
  < Component_Seeded
  < Component_Storage
  < Component_Supertyped
  < Component_Tasks
  < Component_Zoned
  < ObjectWrapperTail<ObjectType_Colony>
  > > > > > > > > > > > > > > > > > > > >
  ColonyBaseT;

AutoClassDerived(Colony, ColonyBaseT,
  Pointer<Planet>, planet,
  Quantity, population,
  Vector<Item>, localItems)

  DERIVED_TYPE_EX(Colony)
  POOLED_TYPE

  /* What the colony looks like: its sky and its terrain, which the client makes. Null without
     one (ADR-016). */
  Renderable visual;

  Colony() {}

  Colony(Object const& planet, Quantity const& population) :
    planet((Planet*)(ObjectT*)planet),
    population(population)
    {}

  void BeginDrawInterior(DrawState* state) {
    Game::Draw("Colony", Game::DrawPhase::BeginInterior, this, state);
  }

  void EndDrawInterior(DrawState* state) {
    Game::Draw("Colony", Game::DrawPhase::EndInterior, this, state);
  }

  Signature GetSignature() const {
    return Signature(100, 2, 0.25f, 0.75f);
  }

  Widget GetWidget(Player const& self) {
    Widget widget;
    ScriptFunction_Load("Object/Widget/Colony:Create")
      ->Call(widget, self, (Object)this);
    return widget;
  }

  void Initialize() {
    Game::CreateVisual("Colony", this, visual);

    Cargo.capacity = FLT_MAX;
    Dockable.ports.push(Bound3(V3(0, 100, 0)));
    Dockable.hangars.push(Bound3(SpatialFn(0.5f) + V3(0, 100, 0)));
    Renderable shape;
    Game::CreateSeededVisual("Asteroid", 1, 0, shape);
    Drawable.renderable = shape;
    Interior.allowMovement = false;
    Zoned.region = SDF_Sphere(0, 500);

    RNG rg = RNG_MTG(GetSeed());
    int itemCount = rg->GetInt(1, 5);
    for (int i = 0; i < itemCount; ++i) {

    }
  }

  void OnDrawInterior(DrawState* state) {
    Game::Draw("Colony", Game::DrawPhase::Interior, this, state);
  }

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);
  }
};

#endif
