// NeuronServer.cpp -- placeholder translation unit for the NeuronServer static library.
//
// The Visual Studio wizard's fnNeuronServer() stub is gone. What is left is one exported symbol,
// which is not ceremony: MSVC emits LNK4221 for an object file with no public symbols, the
// project sets TreatLibWarningAsErrors, and an empty library therefore fails the build. Delete
// this function together with the first real translation unit that takes its place.

#include "pch.h"

namespace Neuron
{

const char* NeuronServerLibraryName() noexcept
{
  return "NeuronServer";
}

} // namespace Neuron
