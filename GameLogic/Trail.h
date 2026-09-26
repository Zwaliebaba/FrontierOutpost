#ifndef Trail_h__
#define Trail_h__

#include "Objects.h"
#include "Attachable.h"
#include "Drawable.h"
#include "Motion.h"
#include "Orientation.h"
#include "Color.h"
#include "Matrix.h"
#include "Meshes.h"
#include "Pool.h"
#include "Renderable.h"
#include "RingBuffer.h"
#include "View.h"
#include "Visual.h"

typedef ObjectWrapper
  < Component_Attachable
  < Component_Drawable
  < Component_Orientation
  < ObjectWrapperTail<ObjectType_Trail>
  > > > >
  TrailBaseT;

/* Not in an anonymous namespace: Trail, which holds these, is in a header now (ADR-016). */
struct SegmentData {
  Position p;
  V3 v;
  float opacity;
};

AutoClassDerived(Trail, TrailBaseT,
  Color, color,
  float, size,
  uint, length,
  float, age)

  DERIVED_TYPE_EX(Trail)
  POOLED_TYPE

  RingBuffer<SegmentData> trail;

  Trail() :
    length(0),
    age(0)
  {
    /* Drawn by the client's visual, where there is a client (ADR-016). */
    Renderable visual;
    Game::CreateVisual("Trail", this, visual);
    Drawable.renderable = visual;
  }

  void OnUpdate(UpdateState& state) {
    BaseType::OnUpdate(state);
    age += state.dt;

    if (!trail.size()) {
      trail.resize(length);
      Position pos = GetPos();
      for (size_t i = 0; i < length; ++i) {
        SegmentData& segment = trail[i];
        segment.p = pos;
        segment.opacity = 0;
      }
    }

    bool faded = true;
    float delta = 1.0f / (float)(length - 1);
    for (size_t i = 0; i < length; ++i) {
      SegmentData& segment = trail.GetRelative(i);
      segment.opacity += delta;
      if (segment.opacity > 0)
        faded = false;
    }

    if (parent && parent->CanMove()) {
      trail.Advance();
      SegmentData& segment = trail.GetCurrent();
      segment.p = GetPos();
      segment.opacity = 0;
    } else if (faded) {
      Delete();
      return;
    }
  }
};

#endif
