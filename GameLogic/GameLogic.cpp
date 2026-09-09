// GameLogic.cpp -- placeholder translation unit for the GameLogic static library.
//
// The Visual Studio wizard's fnGameLogic() stub is gone. What is left is one exported symbol,
// which is not ceremony: MSVC emits LNK4221 for an object file with no public symbols, the
// project sets TreatLibWarningAsErrors, and an empty library therefore fails the build. Delete
// this function together with the first real translation unit that takes its place.
//
// GameLogic is server-side game code and lives in its own namespace, not in Neuron: the engine
// libraries know nothing about this game (AGENTS.md 2).

#include "pch.h"

namespace Frontier
{

const char* GameLogicLibraryName() noexcept
{
  return "GameLogic";
}

} // namespace Frontier
