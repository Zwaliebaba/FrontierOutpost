#include "Items.h"

#include "Materials.h"
#include "Objects.h"
#include "Socket.h"
#include "AttributeIcon.h"
#include "Mass.h"

#include "Array.h"
#include "Model.h"
#include "Script.h"
#include "SDFs.h"
#include "SDFMesh.h"
#include "Transform.h"

#include "Glyphs.h"

const Icon kIcon = Icon_Create()
  ->Add(Glyph_Box(V2(0, 0.8f), V2(1, 0.2f), 1, 1))
  ->Add(Glyph_Arc(V2(0, 0.6f), 0.7f, 0.01f, 1, 1, 0.25f, 0.25f));

typedef
    Attribute_Icon
  < Attribute_Mass
  < ItemWrapper<ItemType_TurretType>
  > >
  TurretTypeBase;

AutoClassDerivedEmpty(TurretType, TurretTypeBase)
  DERIVED_TYPE_EX(TurretType)
  Array<Socket> sockets;

  TurretType() {
    sockets.resize(1);
    sockets[0] = Socket(Transform(), SocketType_Turret, JointType::AxisX);
  }

  Renderable const& GetRenderable() const {
    static Renderable renderable;
    if (!renderable)
      ScriptFunction_Load("Item/TurretType:Generate")->Call(renderable);
    return renderable;
  }

  Array<Socket> const* GetSockets() const {
    return &sockets;
  }

  Object Instantiate(ObjectT* parent = 0) {
    return Object_Turret(this);
  }
};

DERIVED_IMPLEMENT(TurretType)

DefineFunction(Item_TurretType) {
  static Reference<TurretType> self;
  if (!self) {
    self = new TurretType;
    self->icon = kIcon;
    self->mass = 1.0f;
  }
  return self;
}
