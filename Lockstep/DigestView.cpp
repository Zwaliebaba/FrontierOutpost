// DigestView.cpp -- the digest, ranked and grouped for the screen.

#include "pch.h"
#include "DigestView.h"

#include <algorithm>
#include <format>
#include <map>

namespace Lockstep
{

namespace
{

/// A player's name as the rail says it, or an empty string when the index names nobody.
[[nodiscard]] std::string NameOf(const MatchState& _state, OwnerId _owner)
{
  if (_owner == NOBODY || _owner >= static_cast<OwnerId>(_state.players.size()))
  {
    return {};
  }
  return _state.players[static_cast<std::size_t>(_owner)].label;
}

/// `1,610`. The digest counts in thousands often enough to be worth the separator.
[[nodiscard]] std::string Grouped(std::uint32_t _value)
{
  std::string digits = std::to_string(_value);
  for (std::size_t at = digits.size(); at > 3;)
  {
    at -= 3;
    digits.insert(at, ",");
  }
  return digits;
}

} // namespace

std::int32_t ConsequenceRank(EventKind _kind) noexcept
{
  switch (_kind)
  {
  case EventKind::Loss:
    return 0;
  case EventKind::Contact:
    return 1;
  case EventKind::Proposal:
    return 2;
  case EventKind::Custodian:
    return 3;
  case EventKind::Region:
    return 4;
  case EventKind::Economy:
    return 5;
  case EventKind::Ignored:
    return 6;
  default:
    return 7;
  }
}

std::vector<DigestCard> CardsOf(const MatchState& _state)
{
  // ---- Who produced more than one event ----------------------------------------------------------
  //
  // `std::map` and not an unordered one: the grouping order reaches the screen, and an order that
  // depends on a hash is an order that can differ between two builds showing the same tick.
  std::map<OwnerId, std::int32_t> byActor;
  for (const DigestEvent& event : _state.digest)
  {
    if (event.actor != NOBODY && event.actor != _state.viewer)
    {
      ++byActor[event.actor];
    }
  }

  std::vector<DigestCard> cards;
  std::vector<OwnerId> carded;

  for (std::size_t index = 0; index < _state.digest.size(); ++index)
  {
    const DigestEvent& event = _state.digest[index];
    const bool grouped = event.actor != NOBODY && byActor[event.actor] >= 2;

    if (!grouped)
    {
      DigestCard card{.kind = event.kind,
                      .actor = event.actor,
                      .title = event.title,
                      .stamp = {},
                      .lines = {event.detail},
                      .verdict = event.verdict,
                      .verdictDetail = event.verdictDetail,
                      .actions = event.actions,
                      .refs = event.refs,
                      .leadEvent = static_cast<std::int32_t>(index)};
      cards.push_back(std::move(card));
      continue;
    }

    // The actor's first event makes the card; the rest fold into it.
    if (std::ranges::find(carded, event.actor) != carded.end())
    {
      continue;
    }
    carded.push_back(event.actor);

    DigestCard card;
    card.actor = event.actor;
    card.leadEvent = static_cast<std::int32_t>(index);
    card.refs = event.refs;

    // **Ranked by its worst.** A card that led with whichever of a rival's events happened to be
    // first would bury the one that costs you something, which is the opposite of a digest.
    card.kind = EventKind::Ignored;
    std::int32_t count = 0;
    for (std::size_t other = 0; other < _state.digest.size(); ++other)
    {
      const DigestEvent& mine = _state.digest[other];
      if (mine.actor != event.actor)
      {
        continue;
      }

      ++count;
      card.lines.push_back(mine.detail.empty() ? mine.title : mine.detail);
      if (ConsequenceRank(mine.kind) < ConsequenceRank(card.kind))
      {
        card.kind = mine.kind;
        card.refs = mine.refs;
        card.leadEvent = static_cast<std::int32_t>(other);
      }
      if (!mine.verdict.empty())
      {
        card.verdict = mine.verdict;
        card.verdictDetail = mine.verdictDetail;
      }
      for (const EventAction& action : mine.actions)
      {
        card.actions.push_back(action);
      }
    }

    const std::string name = NameOf(_state, event.actor);
    const bool isLeader = !name.empty() && name == _state.player.leader.name;
    card.title = isLeader ? std::format("{} - LEADER {}", name, Grouped(_state.player.leader.score)) : name;
    card.stamp = std::format("{} EVENTS", count);
    cards.push_back(std::move(card));
  }

  // ---- The consequence order -----------------------------------------------------------------------
  //
  // Stable, so that two cards of the same rank keep the order the server put them in -- which is
  // the severity ADR-020 carried, and the better tiebreak than anything decided here.
  std::ranges::stable_sort(cards,
                           [](const DigestCard& _a, const DigestCard& _b) { return ConsequenceRank(_a.kind) < ConsequenceRank(_b.kind); });
  return cards;
}

DigestDelta DeltaOf(const MatchState& _state)
{
  DigestDelta delta;
  if (_state.unreadTicks == 0)
  {
    return delta;
  }

  std::int32_t lost = 0;
  std::int32_t contacts = 0;
  std::int32_t proposals = 0;
  std::int32_t lanes = 0;
  for (const DigestEvent& event : _state.digest)
  {
    switch (event.kind)
    {
    case EventKind::Loss:
      ++lost;
      // A lane dies with the system that anchored it, and the digest says so in the same entry.
      lanes += event.detail.find("ane") != std::string::npos ? 1 : 0;
      break;
    case EventKind::Contact:
      ++contacts;
      break;
    case EventKind::Proposal:
      ++proposals;
      break;
    default:
      break;
    }
  }

  // Only what actually happened. An empty cell is worse than a shorter row: four cells that always
  // read `0 CONTACTS` teach a player to stop reading them.
  if (lost > 0)
  {
    delta.cells.push_back(std::format("-{} SYSTEM{}", lost, lost == 1 ? "" : "S"));
  }
  if (contacts > 0)
  {
    delta.cells.push_back(std::format("{} CONTACT{}", contacts, contacts == 1 ? "" : "S"));
  }
  if (proposals > 0)
  {
    delta.cells.push_back(std::format("{} PROPOSAL{}", proposals, proposals == 1 ? "" : "S"));
  }
  if (lanes > 0)
  {
    delta.cells.push_back(std::format("{} LANE{} LOST", lanes, lanes == 1 ? "" : "S"));
  }
  return delta;
}

} // namespace Lockstep
