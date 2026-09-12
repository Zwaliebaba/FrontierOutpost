// pch.h -- the precompiled header for the client library.
//
// `NeuronClient.h` is where the load-bearing include order lives: NeuronCore first for
// <windows.h>, then d3d12.h and dxgi, which both assume it. Every translation unit here reaches
// the renderer's types, so every one of them needs that prologue.

#ifndef PCH_H
#define PCH_H

#include "NeuronClient.h"

#endif // PCH_H
