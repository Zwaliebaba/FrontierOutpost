#ifndef Component_Projects_h__
#define Component_Projects_h__

#include "ComponentCommon.h"
#include "Project.h"
#include "AutoClass.h"
#include "Vector.h"

AutoClass(ComponentProjects,
  Vector<Project>, elements)

  ComponentProjects() {}
  
  void Run(ObjectT* self, UpdateState& state) {
    for (size_t i = 0; i < elements.size(); ++i)
      elements[i]->Update(state);
  }
};

AutoComponent(Projects)
  void OnUpdate(UpdateState& s) {
    Projects.Run(this, s);
    BaseT::OnUpdate(s);
  }
};

#endif
