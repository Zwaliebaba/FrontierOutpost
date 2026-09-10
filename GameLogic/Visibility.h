#pragma once

#include "MatchState.h"
#include "Protocol.h"
#include "Rules.h"

#include <vector>

namespace Frontier
{

// The fog rule of ADR-017, and the per-seat snapshot of ADR-005.
//
// This file is the only thing in the tree that decides what a seat may see. Nothing on the client
// side filters anything, because a client that filters is a client that was sent what it should not
// have been; the leak, if there is ever one, is here, which is why the tests below it are about
// absence rather than presence.

/// The systems `_seat` observes right now: one it has a fleet at, and anything within
/// `_rules.scoutingRevealLanes` lanes of that.
///
/// A fleet in transit observes nothing. It is on a lane rather than at a system, and a scout that
/// reported from halfway down a lane would make "leaving cedes the system for a tick" a weaker
/// statement than the one-pager's tick resolution intends.
///
/// Indexed by `SystemId`, one entry per system.
[[nodiscard]] std::vector<bool> ObservedSystems(const MatchState& _state, const Rules& _rules, SeatId _seat);

/// Writes what every seat can currently see into that seat's memory.
///
/// Called by the generator so a match opens with each seat knowing its own cluster, and at the end
/// of every resolved tick so that leaving a system converts what was current into what is
/// remembered. It only ever adds or refreshes: fog does not close again, because a player who has
/// seen a system knows it is there.
void ObserveAndRemember(MatchState& _state, const Rules& _rules);

/// What `_seat` may see (ADR-005, ADR-017): every system and lane in the galaxy, contents graded by
/// what that seat has observed, every fleet in transit, and every seat's public standing.
///
/// The snapshot is complete rather than a delta. A seat that has been away for a week is brought
/// fully up to date by one of these and needs no history to be right.
[[nodiscard]] Neuron::VisibleSnapshot VisibleSnapshotFor(const MatchState& _state, const Rules& _rules, SeatId _seat);

} // namespace Frontier
