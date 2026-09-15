#pragma once

#include "MatchState.h"

#include "Protocol.h"

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

/// One state message from the server, decoded -- or refused whole (ADR-044).
struct FreshState
{
  /// False when any part of the message did not decode. **A state that did not decode is not a
  /// state**: the reader fills a short record with zeros and refuses a byte that names no
  /// enumerator, so what came out is not what the server sent and the screen keeps the last state
  /// it could trust.
  bool decoded = false;
  MatchState state;
};

/// A state message and the digests it carries, turned into the next view model (ADR-044).
///
/// **The digests are concatenated, oldest first, and the ones already read are dropped.** Until the
/// server kept more than one there was nothing to concatenate, so a player who closed a lid
/// overnight was told how many ticks they had missed and shown the events of only the last of them.
///
/// `_drawnTick` is the caller's memory of the last tick it drew, and it is what the unread count is
/// measured against. R13 leaves the client nothing to write, so a restarted process passes zero and
/// takes the lot -- honest rather than wrong: it has not looked at any of this.
[[nodiscard]] FreshState StateFrom(const std::vector<std::uint8_t>& _snapshot, const std::vector<Neuron::Protocol::TickDigest>& _digests,
                                   std::uint32_t _drawnTick, std::int64_t _secondsToLock);

/// The orders rail, as an order set the server will accept.
///
/// Reads only what the player can have changed: a fleet told to move, a build queued, a proposal
/// answered. Everything else on the screen is something the server told the client, and sending it
/// back would be the client asserting state it does not own.
[[nodiscard]] OrderSet OrdersOf(const MatchState& _state);

} // namespace Lockstep
