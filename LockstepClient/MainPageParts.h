#pragma once

// MainPageParts.h -- what the main page's translation units share, and its header does not say.
//
// `MainPage` is one class defined across one unit per pane (MainPage.cpp), and three things are
// composed by more than one of them: where a system id sits in the view's list, what a system is
// called in the screen's voice, and the disc a place wears. They are here rather than on the class
// because they are about the VIEW MODEL and not about the page's state, and rather than in
// `MatchState.h` because the wording is this screen's.

#include "DesignTokens.h"
#include "MatchState.h"

#include <cstdint>
#include <string>

namespace Lockstep
{

/// The disc in a place sheet's header and on a `PLACES` rail row (ADR-111, ADR-112). A disc rather
/// than the 8px square a fleet wears, because on this screen a place is round and a fleet is not
/// -- the map has drawn them that way since ADR-079.
inline constexpr float PLACE_DISC_SIZE = 10.0F;

/// Where a system with this id sits in the view's own list, or NONE.
///
/// **A system id and a position in `graph.systems` are different numbers** (ADR-057): the graph is
/// fogged, so the tenth system a player can see is not system ten. A `BuildRow` carries the id,
/// because an order names one; everything the screen focuses names a position.
[[nodiscard]] inline std::int32_t PositionOfSystem(const MatchState& _state, std::int32_t _systemId) noexcept
{
  for (std::size_t index = 0; index < _state.graph.systems.size(); ++index)
  {
    if (_state.graph.systems[index].id == _systemId)
    {
      return static_cast<std::int32_t>(index);
    }
  }
  return EventRefs::NONE;
}

/// A system's name in the screen's voice, or `THE DARK` for a position the graph does not have.
[[nodiscard]] inline std::string NameOfSystem(const MatchState& _state, std::int32_t _at)
{
  return _at >= 0 && _at < static_cast<std::int32_t>(_state.graph.systems.size())
           ? Uppercased(_state.graph.systems[static_cast<std::size_t>(_at)].name)
           : std::string{"THE DARK"};
}

} // namespace Lockstep
