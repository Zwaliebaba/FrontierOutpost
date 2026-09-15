// LinkStatus.cpp -- the four dialogs, chosen from the link and the match.

#include "pch.h"
#include "LinkStatus.h"

#include "MainPage.h"

#include <algorithm>
#include <format>
#include <vector>

namespace Lockstep
{

LinkStatus StatusFor(const LinkFacts& _link, const MatchState& _state, bool _everHadState, bool _finishedDismissed)
{
  LinkStatus status;
  ConnectionDialog::Facts& facts = status.facts;
  facts.server = _link.server;
  facts.reason = _link.refusal;
  facts.seat = _link.seat;
  facts.reconnects = _link.reconnects;
  facts.secondsToNextAttempt = _link.secondsToNextAttempt;

  // No `BACK`: the join screen is behind the seats screen and a whole match, and for the host
  // there is no join screen to return to at all. `QUIT` is the honest button here.
  facts.canGoBack = false;

  if (_link.status == MatchConnection::Status::Refused)
  {
    status.kind = ConnectionDialog::Kind::Refused;
  }
  else if (_link.status == MatchConnection::Status::Lost || _link.status == MatchConnection::Status::Connecting ||
           _link.status == MatchConnection::Status::Resolving)
  {
    // A reconnect passes through `Connecting` on its way back, and from the player's side that
    // is still the link being down. Letting the dialog blink out for the length of a handshake
    // and back in would read as the connection returning and going again.
    status.kind = ConnectionDialog::Kind::Lost;
    facts.lockCountdown = !_everHadState ? std::string{} : MainPage::FormatCountdown(_state.match.secondsToLock);
    facts.lockedTick = _state.OrdersTick();
  }
  else if (!_everHadState)
  {
    // Welcomed, and nothing has ever arrived. Either the host has not started the match or the
    // first state is still in flight -- and from where the player is sitting those are the same
    // thing, so one screen covers both and it resolves the moment a state arrives.
    status.kind = ConnectionDialog::Kind::Waiting;
  }
  else if (_state.match.finished && !_finishedDismissed)
  {
    status.kind = ConnectionDialog::Kind::Finished;

    // **The whole table** (ADR-097). Every player's placement, name and score is already on the
    // wire in `SnapshotStanding` and already in `MatchState::players`, so the final screen can
    // say how the match went rather than only how the reader did.
    //
    // Composed here because this is where `MatchState` and `ConnectionDialog` meet: the dialog is
    // in `LockstepClient` and has no idea what a match is (ADR-038).
    // `standings` is cleared rather than reserved: clearing keeps the capacity a previous frame
    // already paid for, and this runs every frame the finished dialog is up. `table` is a fresh
    // vector each time and does need the one allocation said up front.
    facts.standings.clear();
    std::vector<const PlayerBadge*> table;
    table.reserve(_state.players.size());
    for (const PlayerBadge& badge : _state.players)
    {
      table.push_back(&badge);
    }
    std::ranges::stable_sort(table, [](const PlayerBadge* _a, const PlayerBadge* _b) { return _a->placement < _b->placement; });

    for (const PlayerBadge* badge : table)
    {
      facts.standings.push_back(ConnectionDialog::Facts::Standing{
        .text = std::format("{}  {:<10} {}", MainPage::FormatPlacement(badge->placement), badge->label, badge->score),
        .isYou = badge->isYou});
    }
  }

  return status;
}

} // namespace Lockstep
