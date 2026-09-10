#pragma once

#include "MatchState.h"

namespace Frontier
{

/// The match the main page shows before a server exists.
///
/// It is the state depicted in Design/Screens/README.md: a quiet mid-match, tick 46 resolved,
/// tick 47 locking, 12 players, this player 4th. Every number in it is from the design reference
/// rather than invented, so the built screen and the reference can be compared directly -- which
/// is the only way to tell a layout bug from a data difference.
///
/// It is a FIXTURE and it says so in its name. The day the server sends a digest, this is what
/// the decode fills in, and this function becomes a test fixture rather than the boot path.
[[nodiscard]] MatchState MakeReferenceMatch();

} // namespace Frontier
