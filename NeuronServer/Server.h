// NeuronServer/Server.h
#pragma once

namespace Neuron
{

/// The server's side of the engine. Nothing is here yet: the library exists so that the layers are
/// in place before the server is designed (Design/ADR/ADR-014). It sees NeuronCore and nothing else.
class Server
{
public:
  [[nodiscard]] static const char* Name() noexcept;
};

} // namespace Neuron
