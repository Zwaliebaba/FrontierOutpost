#pragma once

#include "MatchState.h"

#include <string>
#include <vector>

namespace Frontier
{

/// The digest, ranked and grouped for the screen.
///
/// **This is presentation and nothing else.** The server decides what happened and says so in its
/// own words (ADR-020 carries a severity with every entry); this decides what the player reads
/// first and what sits with what. Nothing here changes an order, and nothing here can disagree
/// with the game — the worst it can do is put a true sentence in the wrong place.
///
/// It lives beside `MainPage` rather than inside it because ranking and grouping are arithmetic
/// over data, and arithmetic that is tangled into a draw call is arithmetic nobody can look at.
/// `MainPage` asks for cards and draws them.
///
/// **One tension to know about.** ADR-020 has the digest sorted by a severity the resolver
/// carries, and `DESIGN-GUIDELINES.md` specifies a consequence order computed here. They agree
/// today because the consequence order below was written from the same list. If they ever
/// disagree, the player sees this order and the instrumentation log records the other one.
struct DigestCard
{
  /// Which colour the card wears. For an actor card, the kind of its worst event.
  EventKind kind = EventKind::Economy;

  /// Set when this card groups a player's events. `NOBODY` on a plain event card.
  OwnerId actor = NOBODY;

  /// `PELL LOST TO SORNE` or `HALVORSEN - LEADER 1,610`.
  std::string title;
  /// The right-hand label: `T45` on an event, `3 EVENTS` on an actor card.
  std::string stamp;

  /// The detail, one line per event the card carries.
  std::vector<std::string> lines;

  std::string verdict;
  std::string verdictDetail;

  std::vector<EventAction> actions;
  EventRefs refs;

  /// The digest index this card leads with, so a tap can focus what the server pointed at.
  std::int32_t leadEvent = EventRefs::NONE;
};

/// Where an event sits in the consequence order (DESIGN-GUIDELINES "Copy"): system lost, then a
/// contact, then a proposal, then absence, then the region's timer, then income, then silence.
/// Lower sorts first.
[[nodiscard]] std::int32_t ConsequenceRank(EventKind _kind) noexcept;

/// Ranked, grouped, and ready to draw.
///
/// A player who produced two or more events in the window collapses into one actor card, ranked by
/// their worst, because six lines about one rival is one thing happening rather than six.
[[nodiscard]] std::vector<DigestCard> CardsOf(const MatchState& _state);

/// The four-cell delta above the digest: `-1 SYSTEM`, `1 CONTACT`, `2 PROPOSALS`, `1 LANE LOST`.
///
/// **It summarises the digest in hand, which is the latest tick only.** The header above it says
/// `SINCE YOU LOOKED - T43 > T46`, and until the server retains more than one digest (ADR-028's
/// open question, still owed) those two spans are not the same span. The cells are honest about
/// what they counted; the header is honest about what was missed.
struct DigestDelta
{
  std::vector<std::string> cells;
  [[nodiscard]] bool Any() const noexcept
  {
    return !cells.empty();
  }
};

[[nodiscard]] DigestDelta DeltaOf(const MatchState& _state);

} // namespace Frontier
