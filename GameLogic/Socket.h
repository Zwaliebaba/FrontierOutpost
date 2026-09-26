#ifndef Socket_h__
#define Socket_h__

#include "Item.h"
#include "AutoClass.h"
#include "Transform.h"

namespace JointType {
  enum Enum {
    AxisX,
    AxisY,
    AxisZ,
    Fixed,
    Free
  };
}

AutoClass(Socket,
  Transform, transform,
  SocketType, type,
  JointType::Enum, joint)

  Socket() {}

  Socket(Transform const& transform, SocketType type) :
    transform(transform),
    type(type),
    joint(JointType::Free)
    {}
};

#endif
