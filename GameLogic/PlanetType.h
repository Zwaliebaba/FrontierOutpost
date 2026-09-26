#ifndef PlanetType_h__
#define PlanetType_h__

#include "Items.h"

#include "Objects.h"
#include "Planet.h"
#include "Docks.h"
#include "AttributeIcon.h"
#include "Name.h"
#include "AttributeRenderable.h"
#include "AttributeScale.h"
#include "Seed.h"

/* In a header so that the client can read the type as it makes its look
   (FrontierOutpost/PlanetTypeVisual.cpp, ADR-016). */

typedef
    Attribute_Docks
  < Attribute_Icon
  < Attribute_Name
  < Attribute_Renderable
  < Attribute_Scale
  < Attribute_Seed
  < ItemWrapper<ItemType_PlanetType>
  > > > > > >
  PlanetTypeBase;

AutoClassDerived(PlanetType, PlanetTypeBase,
  float, atmoDensity,
  V3, atmoTint,
  float, cloudLevel,
  Color, color1,
  Color, color2,
  V3, wavelength)

  DERIVED_TYPE_EX(PlanetType)
  PlanetType() {}

  Object Instantiate(ObjectT* parent) {
    return Object_Planet(this);
  }
};

#endif
