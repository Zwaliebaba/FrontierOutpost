// MatchState.cpp -- the two pieces of the view model that are not plain data.
//
// The owner colours ARE the semantic colours (Design/Screens/README.md, *Design tokens*): blue is
// you and also accept and also a trade lane; amber is a rival and also warning and also a
// countdown; coral is a rival and also loss. That economy is deliberate -- a player learns three
// colours instead of six -- and it is why this file is the only place either meaning is decided.

#include "pch.h"
#include "MatchState.h"

namespace Lockstep
{

namespace
{

/// The blue that is always *you*, and is also accept, and is also a trade lane.
constexpr Neuron::Color YOU = {94, 196, 255, Neuron::OPAQUE_ALPHA};

/// Neutral and custodian territory. The same colour on purpose: to everybody else they are the
/// same thing, which is territory nobody is currently defending (one-pager, *Player states*).
constexpr Neuron::Color UNHELD = {214, 220, 228, 115};

/// Twelve rival colours, indexed by player id. ADR-027 argues them.
///
/// **None of these is the blue above.** A viewer sees their own systems blue and every rival in
/// that rival's entry here, so the entry matching the viewer's own id is unused by them and used by
/// everybody else -- which is what makes a given empire look the same on every screen but one.
///
/// The order is not arbitrary. Twelve hues told apart at 8px on black is genuinely hard, and the
/// mitigation is structural rather than chromatic: the galaxy is a ring, the generator places
/// capitals in player order around it, so **adjacent player ids are adjacent empires**. The
/// sequence below alternates warm and cool so that the colours a player most needs to tell apart --
/// their own two neighbours -- are the furthest apart in hue. Distant empires may look similar, and
/// that costs less.
constexpr std::array<Neuron::Color, 12> RIVAL_COLORS = {{
  {255, 196, 87, Neuron::OPAQUE_ALPHA},  // amber   -- the reference's Halvorsen
  {72, 201, 192, Neuron::OPAQUE_ALPHA},  // teal
  {255, 110, 96, Neuron::OPAQUE_ALPHA},  // coral   -- the reference's Sorne
  {186, 148, 255, Neuron::OPAQUE_ALPHA}, // violet
  {198, 222, 96, Neuron::OPAQUE_ALPHA},  // lime
  {216, 132, 224, Neuron::OPAQUE_ALPHA}, // orchid
  {214, 124, 74, Neuron::OPAQUE_ALPHA},  // rust
  {118, 208, 138, Neuron::OPAQUE_ALPHA}, // jade
  {255, 150, 190, Neuron::OPAQUE_ALPHA}, // rose
  {168, 236, 214, Neuron::OPAQUE_ALPHA}, // mint
  {228, 206, 150, Neuron::OPAQUE_ALPHA}, // sand
  {178, 118, 190, Neuron::OPAQUE_ALPHA}, // plum
}};

} // namespace

Neuron::Color OwnerColor(OwnerId _owner, OwnerId _viewer) noexcept
{
  if (_owner == NOBODY)
  {
    return UNHELD;
  }
  if (_owner == _viewer)
  {
    return YOU;
  }

  // Wrapped rather than clamped. A player id past the table is a bug rather than a state, and
  // wrapping gives two empires the same colour where clamping would give every one of them the
  // last colour -- the first is confusing and the second is unreadable.
  const std::size_t index = static_cast<std::size_t>(_owner) % RIVAL_COLORS.size();
  return RIVAL_COLORS[index];
}

std::int32_t RivalColorCount() noexcept
{
  return static_cast<std::int32_t>(RIVAL_COLORS.size());
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

} // namespace Lockstep
