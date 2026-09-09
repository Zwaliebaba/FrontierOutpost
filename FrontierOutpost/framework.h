#pragma once

// Headers every translation unit in the executable needs.
//
// NeuronCore.h is the umbrella header: it owns the whole Windows macro family (NOMINMAX,
// WIN32_LEAN_AND_MEAN, NODRAWTEXT, NOGDI, NOBITMAP, NOMCX, NOSERVICE, NOHELP) and pulls in
// <windows.h> after them. Including it here rather than repeating the macros is the point: two
// owners of one macro is C4005, and /WX makes that fatal. The .vcxproj files deliberately define
// none of them.

#include "targetver.h"

#include "NeuronCore.h"
