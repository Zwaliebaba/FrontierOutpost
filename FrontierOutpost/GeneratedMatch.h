#pragma once

#include "MatchState.h"

#include "MatchRules.h"

#include <cstdint>

namespace Frontier
{

/// A match on a freshly generated galaxy, at tick zero.
///
/// This is the OTHER boot path, beside `MakeReferenceMatch`. The fixture shows the design
/// reference's mid-match so the built screen can be diffed against the drawing; this shows a real
/// galaxy out of `GameLogic::GalaxyGenerator`, so the generator's design-space layout can be looked
/// at on the tilted plane before anything is built on top of it.
///
/// **The rails are empty, and that is the honest answer.** A galaxy that has just been generated
/// has no resolved tick, so there is no digest; no fleets have been built, so there are none to
/// draw; nobody has proposed anything. Filling those with fixture prose would put a contact report
/// about `Kepler-Reach` next to a map with no such system, which is how a screen starts lying about
/// what the simulation has done. They fill in as steps 3 onward of `Design/Plans/4X-01-CoreLoop.md`
/// give them something to say.
///
/// THIS IS WHERE THE SEAM IS. `GameLogic` is server-side and only the executable links it
/// (AGENTS.md §2), so the conversion from a `Galaxy` to the client's `Graph` happens here, in the
/// composition root, and `MainPage` goes on knowing nothing but `MatchState`.
[[nodiscard]] MatchState MakeGeneratedMatch(const MatchRules& _rules, std::uint64_t _seed);

} // namespace Frontier
