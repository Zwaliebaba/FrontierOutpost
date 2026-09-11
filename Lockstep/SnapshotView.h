#pragma once

#include "MatchState.h"

#include "Orders.h"
#include "Snapshot.h"
#include "TickLog.h"

#include <cstdint>
#include <vector>

namespace Lockstep
{

/// The two conversions between what the server sends and what the screen reads.
///
/// **This file is the seam from the client's side, and it is the only place that sees both.** The
/// client never links `GameLogic` (AGENTS.md §2) and `MainPage` knows nothing but `MatchState`;
/// this lives in the executable, which is the composition root and the one thing entitled to see
/// each half.
///
/// The `MatchState` that comes out is not a decoded `Snapshot` -- it is a *rendering* of one. The
/// snapshot is what the player is entitled to know; the view model is that plus everything the
/// screen needs to draw it, including strings the server has no business composing.
[[nodiscard]] MatchState ViewOf(const Snapshot& _snapshot, const std::vector<DigestEntry>& _digest, std::int64_t _secondsToLock);

/// The orders rail, as an order set the server will accept.
///
/// Reads only what the player can have changed: a fleet told to move, a build queued, a proposal
/// answered. Everything else on the screen is something the server told the client, and sending it
/// back would be the client asserting state it does not own.
[[nodiscard]] OrderSet OrdersOf(const MatchState& _state);

} // namespace Lockstep
