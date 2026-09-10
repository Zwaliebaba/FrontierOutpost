#pragma once

// NeuronServer.h -- the umbrella header for the server library.
//
// NeuronCore.h is the only thing this library builds on, and that is a rule rather than a
// convenience: NeuronServer references NeuronCore and nothing else, so the game's rules stay on
// the far side of the Simulation seam and the server ticks a simulation without knowing what one
// is (AGENTS.md 2, ADR-007).
//
// What this library actually uses from it is Debug.h -- ASSERT_TEXT, and the one error path this
// tree has -- and the Windows macro family NeuronCore.h owns (NOMINMAX, WIN32_LEAN_AND_MEAN,
// NODRAWTEXT, NOGDI, NOBITMAP, NOMCX, NOSERVICE, NOHELP), which it defines before pulling in
// <windows.h>. The macros are not repeated here and the .vcxproj defines none of them, because
// two owners of one macro is C4005 and /WX makes that fatal (AGENTS.md 4).

#include "NeuronCore.h"
