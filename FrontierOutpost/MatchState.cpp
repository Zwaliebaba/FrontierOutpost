// MatchState.cpp -- the two pieces of the view model that are not plain data.

#include "pch.h"
#include "MatchState.h"

namespace Frontier
{

Neuron::Color OwnerColor(Owner _owner) noexcept
{
  // The owner colours ARE the semantic colours (Design/Screens/README.md "Design tokens"): blue
  // is you and also accept and also a trade lane; amber is Halvorsen and also warning and also a
  // countdown; red is Sorne and also loss. That is a deliberate economy -- a player learns three
  // colours instead of six -- and it is why this switch is the only place either meaning is
  // decided.
  switch (_owner)
  {
  case Owner::You:
    return Neuron::Color{94, 196, 255, Neuron::OPAQUE_ALPHA};
  case Owner::Halvorsen:
    return Neuron::Color{255, 196, 87, Neuron::OPAQUE_ALPHA};
  case Owner::Sorne:
    return Neuron::Color{255, 110, 96, Neuron::OPAQUE_ALPHA};
  case Owner::Neutral:
  default:
    // Neutral and custodian systems share a colour, because to everyone else they are the same
    // thing: territory nobody is currently defending (one-pager, "Player states").
    return Neuron::Color{214, 220, 228, 115};
  }
}

std::int32_t Graph::FindSystem(std::string_view _name) const noexcept
{
  for (std::size_t index = 0; index < systems.size(); ++index)
  {
    if (systems[index].name == _name)
    {
      return static_cast<std::int32_t>(index);
    }
  }
  return EventRefs::NONE;
}

} // namespace Frontier
