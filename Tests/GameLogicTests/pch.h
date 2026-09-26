// Tests/GameLogicTests/pch.h
//
// The precompiled header. It leaves <windows.h> out: the game names DrawState, which windows.h
// defines as a macro (Design/ADR/ADR-005), and these tests need nothing of Windows.
#pragma once

#include <CppUnitTest.h>
