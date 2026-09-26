#ifndef Component_Missions_h__
#define Component_Missions_h__

#include "ComponentCommon.h"
#include "Mission.h"
#include "AutoClass.h"

AutoClass(ComponentMissions,
  Vector<Mission>, elements)

  ComponentMissions() {}

  void Run(ObjectT* self, UpdateState& state) {}
};

AutoComponent(Missions)
  void OnUpdate(UpdateState& s) {
    Missions.Run(this, s);
    BaseT::OnUpdate(s);
  }

  void AddMission(Mission const& mission) {
    Missions.elements.push(mission);
  }

  void RemoveMission(Mission const& mission) {
    Missions.elements.remove(mission);
  }
};

#endif
