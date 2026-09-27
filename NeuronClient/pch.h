// NeuronClient/pch.h
//
// The precompiled header, and the one header in NeuronClient that owns the Windows macro family
// (AGENTS.md §4): the macros are defined here, before <windows.h>, and nowhere else. Only
// NeuronClient's own .cpp files include it. No public header includes a Windows, DXGI or Direct3D
// header (Design/ADR/ADR-005), because liblt defines names those headers take as macros.
#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// winrt::com_ptr owns every COM object (AGENTS.md R12, Design/ADR/ADR-019). unknwn.h comes
// first: base.h supports classic COM interfaces, as Direct3D's are, only once IUnknown is declared,
// and WIN32_LEAN_AND_MEAN keeps windows.h from declaring it.
#include <unknwn.h>
#include <winrt/base.h>
