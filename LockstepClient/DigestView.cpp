// DigestView.cpp -- the digest, ranked and grouped for the screen.

#include "pch.h"
#include "DigestView.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <map>
#include <utility>

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

/// What a player can do when the digest has nothing to act on.
///
/// The standing moves: start a building, and send a fleet somewhere. They are the same actions the
/// events carry mid-match -- this is not a second way to give an order, it is the same way with
/// nothing to hang it on.
///
/// **Not only at the opening** (ADR-056). A tick can resolve, be reported, and carry nothing to
/// act on: a production line with every held system already built is the common one, and it leaves
/// a digest of one card and no buttons while the player still has fleets standing and credits in
/// hand. The orders rail says "Change it from the digest", so a digest with no control on it is
/// the screen saying there is nothing to do when there is.
[[nodiscard]] std::vector<EventAction> StandingMoves(const MatchState& _state)
{
  /// Two fleets' worth, because the digest column fits about that many buttons beside a build and
  /// a player with six fleets does not need six of them here -- the map is where a fleet is picked
  /// when the choice is which fleet.
  constexpr std::size_t MOST_FLEETS_OFFERED = 2;

  std::vector<EventAction> actions;

  // The first row that can actually be STARTED, which is not always the first row: a system already
  // building is on the list so the sheet and the rail can say what is rising there, and offering it
  // would be a button the lock is certain to refuse (ADR-069, ADR-070).
  const auto startable =
    std::find_if(_state.orders.builds.begin(), _state.orders.builds.end(), [](const BuildRow& _row) { return !_row.rising; });
  if (startable != _state.orders.builds.end())
  {
    // Priced like every other build button (ADR-053).
    actions.push_back(EventAction{.label = std::format("BUILD {} CR", startable->cost),
                                  .kind = EventActionKind::QueueBuild,
                                  .target = static_cast<std::int32_t>(std::distance(_state.orders.builds.begin(), startable)),
                                  .primary = true});
  }

  std::size_t offered = 0;
  for (std::size_t index = 0; index < _state.fleets.size() && offered < MOST_FLEETS_OFFERED; ++index)
  {
    const Fleet& fleet = _state.fleets[index];
    if (fleet.owner != _state.viewer)
    {
      continue;
    }

    // A fleet already under way is not redirectable -- `Match::Validate` refuses it, and a button
    // whose order the lock is certain to refuse is the thing ADR-053 took off this screen. A fleet
    // the player ordered somewhere THIS tick is not that: the lock has not come, so it is still
    // standing where it stands and the button re-opens the picker on it (ADR-077).
    if (fleet.underWay)
    {
      continue;
    }

    actions.push_back(EventAction{.label = "MOVE " + fleet.name,
                                  .kind = EventActionKind::RedirectFleet,
                                  .target = static_cast<std::int32_t>(index),
                                  .primary = actions.empty()});
    ++offered;
  }
  return actions;
}

/// Replaces a card's `MAP` buttons with the names of the systems they point at, and drops the one
/// that points where the card already goes (ADR-081).
///
/// **Four `MAP` buttons on one card is four identical labels for four different places**, and the
/// card that carried them -- `P4 · 5 EVENTS` -- says neither what P4 did nor where. A chip per
/// distinct system, named, is the same targets with the answer written on them.
///
/// **And a `MAP` that focuses what tapping the card already focuses is a button that does nothing
/// visible.** Every plain event card had one: `EventRefs::system` is what the card body focuses and
/// what the button targeted, so the two were the same tap drawn twice.
void NameTheTargets(const MatchState& _state, DigestCard& _card)
{
  /// Four fits beside a real control at this width, and a card whose rival touched five systems is
  /// telling a story the per-event lines already carry.
  constexpr std::size_t MOST_CHIPS = 4;

  std::vector<std::int32_t> systems;
  std::vector<EventAction> kept;
  for (const EventAction& action : _card.actions)
  {
    if (action.kind != EventActionKind::Focus)
    {
      kept.push_back(action);
      continue;
    }
    const bool nowhere = action.target < 0 || action.target >= static_cast<std::int32_t>(_state.graph.systems.size());
    if (nowhere || action.target == _card.refs.system)
    {
      continue;
    }
    if (std::ranges::find(systems, action.target) == systems.end())
    {
      systems.push_back(action.target);
    }
  }

  const auto named = [&_state](std::int32_t _system)
  {
    const std::string& name = _state.graph.systems[static_cast<std::size_t>(_system)].name;
    std::string shouted = name.empty() ? std::string{"THE FALLOW"} : name;
    std::transform(shouted.begin(), shouted.end(), shouted.begin(), [](unsigned char _c) { return static_cast<char>(std::toupper(_c)); });
    return shouted;
  };

  for (std::size_t index = 0; index < systems.size() && index < MOST_CHIPS; ++index)
  {
    kept.push_back(EventAction{.label = named(systems[index]), .kind = EventActionKind::Focus, .target = systems[index]});
  }

  // The overflow chip goes to the first one it stands for rather than nowhere: a control that says
  // there is more and then does nothing when pressed is the defect this screen keeps being bitten
  // by. The rest are on the card's own lines.
  if (systems.size() > MOST_CHIPS)
  {
    kept.push_back(
      EventAction{.label = std::format("+{}", systems.size() - MOST_CHIPS), .kind = EventActionKind::Focus, .target = systems[MOST_CHIPS]});
  }

  _card.actions = std::move(kept);
}

/// Whether this event may be folded into a run of the same thing (ADR-062).
///
/// **Never a contact, a capture or a proposal.** Each of those is a consequence, and a consequence
/// reported once with a bigger number is a consequence the player was not told about -- two rivals
/// arriving at two systems is not one arrival. Nor anything carrying a verdict, which is a fight
/// and so the least foldable thing in the digest.
[[nodiscard]] bool CanMerge(const DigestEvent& _event) noexcept
{
  const bool consequence = _event.kind == EventKind::Contact || _event.kind == EventKind::Loss || _event.kind == EventKind::Proposal;
  return !consequence && _event.verdict.empty();
}

/// A title split into the words in front and the SIGNED number at the end: `Production +6` is
/// `Production ` and 6.
///
/// **The sign is what makes this safe.** It is the difference between a quantity a run can be
/// summed into and a number that happens to end a name -- `Claimed Vega 7` twice is not
/// `Claimed Vega 14` -- so a title whose trailing digits are not introduced by `+` or `-` reports
/// no count at all and can only merge with a title identical to it.
[[nodiscard]] bool SplitCount(const std::string& _title, std::string& _outStem, std::int64_t& _outCount)
{
  std::size_t digits = _title.size();
  while (digits > 0 && _title[digits - 1] >= '0' && _title[digits - 1] <= '9')
  {
    --digits;
  }
  if (digits == _title.size() || digits == 0)
  {
    return false;
  }

  const char sign = _title[digits - 1];
  if (sign != '+' && sign != '-')
  {
    return false;
  }

  std::int64_t value = 0;
  const std::from_chars_result parsed = std::from_chars(_title.data() + digits, _title.data() + _title.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != _title.data() + _title.size())
  {
    return false;
  }

  _outStem = _title.substr(0, digits - 1);
  _outCount = sign == '-' ? -value : value;
  return true;
}

/// Whether `_next` is another go at the thing `_previous` already reported.
[[nodiscard]] bool Repeats(const DigestEvent& _previous, const DigestEvent& _next)
{
  if (!CanMerge(_previous) || !CanMerge(_next) || _previous.kind != _next.kind || _previous.actor != _next.actor ||
      _previous.refs.system != _next.refs.system)
  {
    return false;
  }

  std::string wasStem;
  std::string isStem;
  std::int64_t was = 0;
  std::int64_t is = 0;
  if (SplitCount(_previous.title, wasStem, was) && SplitCount(_next.title, isStem, is))
  {
    return wasStem == isStem;
  }
  return _previous.title == _next.title;
}

/// The digest with each run of repeats folded into one event, and the digest index each of them
/// leads with so a tap still focuses what the server pointed at.
///
/// **Only for a player who was away** (ADR-062). Within one tick a repeat is two different things
/// that read alike; across four it is one thing said four times, and the header above already
/// frames the whole window as one span.
void MergeRepeats(const MatchState& _state, std::vector<DigestEvent>& _outEvents, std::vector<std::int32_t>& _outSource)
{
  std::vector<std::pair<std::size_t, std::size_t>> runs;
  for (std::size_t index = 0; index < _state.digest.size(); ++index)
  {
    const bool folds = _state.unreadTicks >= 2 && !runs.empty() && Repeats(_state.digest[runs.back().second], _state.digest[index]);
    if (folds)
    {
      runs.back().second = index;
      continue;
    }
    runs.emplace_back(index, index);
  }

  for (const auto& [first, last] : runs)
  {
    DigestEvent merged = _state.digest[first];
    _outSource.push_back(static_cast<std::int32_t>(first));

    if (last > first)
    {
      // The sum, when the run counts something, and the newest detail either way: `154 credits in
      // hand` is a running total, so the one that is still true is the last.
      std::string stem;
      std::int64_t total = 0;
      if (SplitCount(merged.title, stem, total))
      {
        for (std::size_t index = first + 1; index <= last; ++index)
        {
          std::string other;
          std::int64_t value = 0;
          if (SplitCount(_state.digest[index].title, other, value))
          {
            total += value;
          }
        }
        merged.title = std::format("{}{}{}", stem, total < 0 ? '-' : '+', total < 0 ? -total : total);
      }
      merged.title += std::format(" - T{} > T{}", _state.lastSeenTick, _state.match.tick);
      merged.detail = _state.digest[last].detail;

      // Every action the run offered, once each. A build is offered on exactly one card (ADR-057),
      // and folding two cards into one must not drop the second's button or draw two filled ones.
      for (std::size_t index = first + 1; index <= last; ++index)
      {
        for (const EventAction& action : _state.digest[index].actions)
        {
          const bool already = std::ranges::any_of(merged.actions, [&action](const EventAction& _mine)
                                                   { return _mine.kind == action.kind && _mine.target == action.target; });
          if (already)
          {
            continue;
          }
          const bool primaryTaken = std::ranges::any_of(merged.actions, [](const EventAction& _mine) { return _mine.primary; });
          EventAction carried = action;
          carried.primary = carried.primary && !primaryTaken;
          merged.actions.push_back(std::move(carried));
        }
      }
    }

    _outEvents.push_back(std::move(merged));
  }
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
  // ---- Nothing to report, which is not the same as nothing to do ---------------------------------
  //
  // Two different empties and they want different words: before the first lock nothing has
  // resolved, and after one an empty digest is a fact about the fog rather than about the tick.
  // Both carry the opening moves, unless the match is over, when there are none.
  if (_state.digest.empty())
  {
    const bool beforeTheFirstLock = _state.match.tick == 0;
    DigestCard card;
    card.kind = EventKind::Economy;
    card.title = beforeTheFirstLock ? "NOTHING HAS HAPPENED YET" : "A QUIET TICK";
    card.lines.push_back(beforeTheFirstLock
                           ? "The first tick resolves when the countdown ends. What you order before then is what it resolves."
                           : "Nothing you could see changed. Systems you have not scouted may have.");
    if (!_state.orders.locked)
    {
      card.actions = StandingMoves(_state);
    }
    return {card};
  }

  // ---- The same thing, said once -----------------------------------------------------------------
  //
  // Runs of repeats are folded before anything is ranked or grouped (ADR-062), so a fold counts as
  // one event everywhere below -- including in the actor count, where three production lines about
  // one rival should not be the reason their card exists.
  std::vector<DigestEvent> events;
  std::vector<std::int32_t> source;
  MergeRepeats(_state, events, source);

  // ---- Who produced more than one event ----------------------------------------------------------
  //
  // `std::map` and not an unordered one: the grouping order reaches the screen, and an order that
  // depends on a hash is an order that can differ between two builds showing the same tick.
  std::map<OwnerId, std::int32_t> byActor;
  for (const DigestEvent& event : events)
  {
    if (event.actor != NOBODY && event.actor != _state.viewer)
    {
      ++byActor[event.actor];
    }
  }

  std::vector<DigestCard> cards;
  std::vector<OwnerId> carded;

  for (std::size_t index = 0; index < events.size(); ++index)
  {
    const DigestEvent& event = events[index];
    const bool grouped = event.actor != NOBODY && byActor[event.actor] >= 2;

    if (!grouped)
    {
      // **`NOBODY`, not `event.actor`.** `DigestCard::actor` means "this card GROUPS a player's
      // events", which is what the header says and what a reader would assume; copying the actor
      // onto an ungrouped card made `actor != NOBODY` untrue as a test for one. Nothing drew from
      // it yet, which is exactly why it was worth fixing before something did -- the obvious use is
      // an owner swatch, and it would have gone on cards that are not about that owner.
      DigestCard card{.kind = event.kind,
                      .actor = NOBODY,
                      .title = event.title,
                      .stamp = {},
                      .lines = {event.detail},
                      .verdict = event.verdict,
                      .verdictDetail = event.verdictDetail,
                      .actions = event.actions,
                      .refs = event.refs,
                      .leadEvent = source[index]};
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
    card.leadEvent = source[index];
    card.refs = event.refs;

    // **Ranked by its worst.** A card that led with whichever of a rival's events happened to be
    // first would bury the one that costs you something, which is the opposite of a digest.
    card.kind = EventKind::Ignored;
    std::int32_t count = 0;
    for (std::size_t other = 0; other < events.size(); ++other)
    {
      const DigestEvent& mine = events[other];
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
        card.leadEvent = source[other];
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

  // ---- What each card points at ----------------------------------------------------------------
  //
  // After the cards are assembled and before they are ranked, because it is about one card at a
  // time and a card is not finished until its events have all folded into it (ADR-081).
  for (DigestCard& card : cards)
  {
    NameTheTargets(_state, card);
  }

  // ---- The consequence order -----------------------------------------------------------------------
  //
  // Stable, so that two cards of the same rank keep the order the server put them in -- which is
  // the severity ADR-020 carried, and the better tiebreak than anything decided here.
  std::ranges::stable_sort(cards,
                           [](const DigestCard& _a, const DigestCard& _b) { return ConsequenceRank(_a.kind) < ConsequenceRank(_b.kind); });

  // ---- A digest that reports something but offers nothing ------------------------------------------
  //
  // **Nothing to act on is not the same as nothing to do** (ADR-056). FOCUS does not count: it
  // moves the eye and gives no order, so a card carrying only a MAP button is still a card the
  // player cannot do anything with. When no card offers a real control the standing moves go on
  // the leading one, which is where the eye already is.
  const bool anythingToActOn = std::ranges::any_of(
    cards, [](const DigestCard& _card)
    { return std::ranges::any_of(_card.actions, [](const EventAction& _action) { return _action.kind != EventActionKind::Focus; }); });
  if (!anythingToActOn && !cards.empty() && !_state.orders.locked)
  {
    // In front of whatever the card already carried, which is a MAP button at most: the control
    // that gives an order comes before the one that only looks at something.
    const std::vector<EventAction> standing = StandingMoves(_state);
    cards.front().actions.insert(cards.front().actions.begin(), standing.begin(), standing.end());
  }

  return cards;
}

std::string HiddenSummary(const std::vector<DigestCard>& _cards, std::size_t _from)
{
  if (_from >= _cards.size())
  {
    return {};
  }
  const std::size_t hidden = _cards.size() - _from;

  // **A battle outranks its own kind.** A fight is a `Loss` card like a system lost is, and it is
  // the one a player most needs to know is down there -- the verdict is what tells them apart, and
  // `CanMerge` already treats a card carrying one as unfoldable for the same reason.
  std::size_t battles = 0;
  std::int32_t worst = 99;
  for (std::size_t index = _from; index < _cards.size(); ++index)
  {
    battles += _cards[index].verdict.empty() ? 0U : 1U;
    worst = std::min(worst, ConsequenceRank(_cards[index].kind));
  }

  const auto plural = [](std::size_t _count, const char* _one, const char* _many)
  { return std::format("{} {}", _count, _count == 1 ? _one : _many); };

  if (battles > 0)
  {
    return std::format("{} MORE · {}", hidden, plural(battles, "BATTLE", "BATTLES"));
  }

  std::size_t ofThatKind = 0;
  for (std::size_t index = _from; index < _cards.size(); ++index)
  {
    ofThatKind += ConsequenceRank(_cards[index].kind) == worst ? 1U : 0U;
  }

  // The words the digest already uses for these, so the band reads like the cards it is about.
  const char* one = "EVENT";
  const char* many = "EVENTS";
  switch (worst)
  {
  case 0:
    one = "SYSTEM LOST";
    many = "SYSTEMS LOST";
    break;
  case 1:
    one = "CONTACT";
    many = "CONTACTS";
    break;
  case 2:
    one = "OFFER";
    many = "OFFERS";
    break;
  case 3:
    one = "CUSTODIAN";
    many = "CUSTODIANS";
    break;
  case 4:
    one = "REGION";
    many = "REGION";
    break;
  case 5:
    one = "INCOME";
    many = "INCOME";
    break;
  default:
    one = "SILENCE";
    many = "SILENCE";
    break;
  }
  return std::format("{} MORE · {}", hidden, plural(ofThatKind, one, many));
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
    delta.cells.push_back(DeltaCell{std::format("−{} SYSTEM{}", lost, lost == 1 ? "" : "S"), true});
  }
  if (contacts > 0)
  {
    delta.cells.push_back(DeltaCell{std::format("{} CONTACT{}", contacts, contacts == 1 ? "" : "S")});
  }
  if (proposals > 0)
  {
    delta.cells.push_back(DeltaCell{std::format("{} PROPOSAL{}", proposals, proposals == 1 ? "" : "S")});
  }
  if (lanes > 0)
  {
    // **Amber, not red, and that is unchanged rather than decided here.** A lane lost is a loss by
    // any reading, but it has always drawn amber because it never led with a sign, and what the
    // two colours divide in this box is a design question rather than a font one. Carrying the
    // flag makes the current answer visible; it does not answer it.
    delta.cells.push_back(DeltaCell{std::format("{} LANE{} LOST", lanes, lanes == 1 ? "" : "S")});
  }
  return delta;
}

} // namespace Lockstep
