#pragma once

// Headers the precompiled header carries for every translation unit in this library.
//
// NeuronCore.h is the umbrella header: it owns the whole Windows macro family (NOMINMAX,
// WIN32_LEAN_AND_MEAN, NODRAWTEXT, NOGDI, NOBITMAP, NOMCX, NOSERVICE, NOHELP), pulls in
// <windows.h> after them, and brings Debug.h with it -- which is where ASSERT_TEXT and the one
// error path this tree has live. Including it here rather than repeating the macros is the point:
// two owners of one macro is C4005, and /WX makes that fatal. The .vcxproj files deliberately
// define none of them (AGENTS.md 4).

#include "NeuronCore.h"
