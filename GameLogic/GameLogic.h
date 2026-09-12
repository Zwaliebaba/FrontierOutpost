#pragma once

// GameLogic.h -- the umbrella header for the game library.
//
// It brings NeuronCore.h, which is the same single include NeuronServer.h carries and puts every
// library in this tree on one shape: pch.h includes <Library>.h and nothing else. With it comes
// the Windows macro family NeuronCore.h owns and defines before <windows.h>, so the macros are
// not repeated here and the .vcxproj defines none of them -- two owners of one macro is C4005,
// and /WX makes that fatal (AGENTS.md 4).
//
// WHAT THIS DOES NOT CHANGE. The simulation is still integer arithmetic end to end: no float, no
// double, no wall clock, and the tick is the clock (R16). That is what makes a World ticked the
// same number of times from the same start land in the same place on any machine, and it is a
// property of the code rather than of what this header includes.
//
// WHAT IT DOES CHANGE. Every translation unit in this library parses <windows.h> and
// <WinSock2.h>. GameLogic contains no platform code and needs
// none of that surface; the include is here for uniformity across the four libraries rather than
// because anything in the simulation asks for it. Design/Reference/mobile-portability.md 3 is the
// place that says what that costs.

#include "NeuronCore.h"
