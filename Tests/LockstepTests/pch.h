// pch.h -- the precompiled header for this test project.
//
// CppUnitTest.h is included by the test translation units rather than here, so that a failure to
// find the unit-test headers points at the file that wanted them.
//
// **`NeuronClient.h` is here and is not in the other three test projects', and it has to be.** The
// client's view model reaches the renderer's types -- `MatchState` carries a `Neuron::Color`,
// `MainPage` takes a `ShapeRenderer` -- and `NeuronClient.h` is where the load-bearing include
// order lives: NeuronCore first for <windows.h>, then d3d12.h and dxgi, which both assume it.
// Without it a test that includes `MainPage.h` fails inside `DescriptorHeap.h` on `ID3D12Device`,
// several headers away from anything it wrote. `Lockstep/pch.h` is this same line for the same
// reason; this project is compiling that code, so it needs that prologue.

#ifndef PCH_H
#define PCH_H

#include "NeuronClient.h"

#endif // PCH_H
