#pragma once

// LinkStatus.h -- which dialog the link and the match together call for.
//
// **The one place `MatchState` and `ConnectionDialog` meet.** The dialog is in this library and has
// no idea what a match is (ADR-038); the page knows nothing but `MatchState`. Deciding between the
// four dialogs needs both, and it was decided inside the composition root's frame loop -- where the
// four conditions were reachable only by launching the client on Windows.
//
// It is here so that they are reachable by a test instead. Nothing in it touches a socket, a window
// or a device.

#include "ConnectionDialog.h"
#include "MatchConnection.h"
#include "MatchState.h"

#include <cstdint>
#include <string>

namespace Lockstep
{

/// What the client knows about its link, as the screens need it: the status, and the five facts the
/// dialog passes straight through from the connection.
///
/// A record rather than the `MatchConnection` itself, because a connection owns a socket and this
/// decision does not -- which is the difference between a rule a test can state and one it cannot.
struct LinkFacts
{
  MatchConnection::Status status = MatchConnection::Status::Idle;
  std::string server;
  Neuron::RefusalReason refusal = Neuron::RefusalReason::None;
  std::int32_t seat = -1;
  std::uint32_t reconnects = 0;
  double secondsToNextAttempt = 0.0;
};

/// Which dialog to show, and everything it needs to say.
struct LinkStatus
{
  ConnectionDialog::Kind kind = ConnectionDialog::Kind::None;
  ConnectionDialog::Facts facts;
};

/// Chosen from the connection and the state together, in one place, so that two of these can never
/// be true at once on the screen.
///
/// **The order is the order of severity**: a refusal is final, a lost link is not, a match with no
/// first state has not started yet, and a finished match is the only one of the four that is not a
/// problem.
///
/// `_everHadState` is whether any state has ever arrived in this process, and `_finishedDismissed`
/// whether the player has already closed the final screen. Both are the composition root's memory:
/// R13 leaves the client nothing to write, so neither survives a restart.
[[nodiscard]] LinkStatus StatusFor(const LinkFacts& _link, const MatchState& _state, bool _everHadState, bool _finishedDismissed);

} // namespace Lockstep
