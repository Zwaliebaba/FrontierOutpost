#pragma once

// Headers the precompiled header carries for every translation unit in this library.
//
// WIN32_LEAN_AND_MEAN and NOMINMAX are defined by the .vcxproj, NOT here. The project definition
// is already in force when this file is parsed, and /D spells a bare macro as 1 while a #define
// here spells it as nothing -- two different definitions of one name, which is C4005, which /WX
// makes fatal. Anything that needs <windows.h> includes it after those macros are set, which the
// project guarantees (see .clang-format on include order).

#include <cstddef>
#include <cstdint>
