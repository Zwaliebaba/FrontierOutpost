#pragma once

#include "Galaxy.h"
#include "MatchRules.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Lockstep
{

/// One side of a fight: who, how many, and whether they were already here.
struct MeleeSide
{
  PlayerId player;
  std::uint32_t ships = 0;
  /// An incumbent gets the defender bonus. It is a property of the FLEET, not of who owns the
  /// system -- at an empty system nobody owns anything and simultaneous arrivals still get nothing
  /// (ADR-021).
  bool incumbent = false;
};

/// Runs the melee and writes the survivors back into `_sides`.
///
/// **This is the only implementation of combat in the tree, and that is the point.** The orders
/// rail previews a fight before it happens -- `Design/Screens/README.md` shows "preview: 14 v 11
/// (+def) - 6 left" -- and a preview computed by a second copy of the arithmetic is a preview that
/// will one day disagree with the fight. `TickResolver` calls this at a system; `Snapshot` calls it
/// on a hypothetical. Neither knows how it works.
///
/// ADR-021 has the reasoning. In one sentence: every side's output is computed from round-start
/// strength and applied together, so nobody fires first and a tie is a tie rather than a race.
///
/// Pure, integer, and free of the match -- it takes rules and numbers and touches nothing else,
/// which is what makes an exact preview possible at all.
void ResolveMelee(const MatchRules& _rules, std::span<MeleeSide> _sides);

} // namespace Lockstep
