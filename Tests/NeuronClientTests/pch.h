// Tests/NeuronClientTests/pch.h
//
// The precompiled header, and the test project's one owner of the Windows macro family
// (AGENTS.md §4): the macros are defined here, before <windows.h>, and nowhere else.
#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wrl/client.h>

#include <CppUnitTest.h>
