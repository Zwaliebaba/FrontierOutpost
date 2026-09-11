#pragma once

#include "MatchRules.h"
#include "Orders.h"
#include "Snapshot.h"

#include <cstdint>

namespace Lockstep
{

/// How a seat plays itself.
///
/// **These were the scripted match's bots and they are the game's now.** They were written to prove
/// the core loop worked end to end before anybody played it, which meant they had to be good enough
/// to reach every mechanic — and that is exactly the bar a seat nobody is sitting in has to clear.
/// Promoting them beat writing a second set: a bot that behaved differently from the one the tests
/// drive would make the suite's whole-match run stop being evidence about the shipped game.
///
/// **They play from the SNAPSHOT and never from the match.** A bot reads the same fogged view a
/// human is sent, so it cannot know what a player could not, and every tie breaks on the lowest id.
/// That is what keeps R16 true of a match with bots in it: the same seed and the same orders give
/// the same match on every machine, and a bot's move is a pure function of what it was shown.
enum class BotPolicy : std::uint8_t
{
  /// Never moves. Builds shipyards on what it holds and defends. `CAUTIOUS` on the seats screen.
  Turtle,
  /// Takes the nearest open ground. `STEADY`.
  ExpandNear,
  /// Takes the furthest open ground it can see, which spreads it thin and meets people sooner.
  ExpandFar,
  /// Goes for somebody else's systems first and open ground only when there are none. `AGGRESSIVE`.
  Raider,
  /// Offers a trade lane wherever its territory touches a rival's. One offer a tick.
  Diplomat,
  /// Does nothing at all. Not offered on the seats screen -- it exists because the scripted match
  /// needs a player who never turns up, to drive the custodian rules.
  Absentee
};

/// The three a host can pick, in the seats screen's words.
[[nodiscard]] constexpr BotPolicy PolicyForStyle(std::uint8_t _style) noexcept
{
  switch (_style)
  {
  case 0:
    return BotPolicy::Turtle;
  case 2:
    return BotPolicy::Raider;
  case 1:
  default:
    return BotPolicy::ExpandNear;
  }
}

[[nodiscard]] constexpr const char* Describe(BotPolicy _policy) noexcept
{
  switch (_policy)
  {
  case BotPolicy::Turtle:
    return "CAUTIOUS";
  case BotPolicy::ExpandNear:
    return "STEADY";
  case BotPolicy::ExpandFar:
    return "RESTLESS";
  case BotPolicy::Raider:
    return "AGGRESSIVE";
  case BotPolicy::Diplomat:
    return "DIPLOMAT";
  case BotPolicy::Absentee:
    return "ABSENT";
  default:
    return "unknown";
  }
}

/// One bot's orders for one tick, from what it can see and nothing else.
///
/// **Deterministic, and that is a requirement rather than a property.** A match with bots in it
/// still has to resolve identically on every machine and replay identically from its store
/// (ADR-024), so nothing here may read a clock, a random number, or the authoritative match.
[[nodiscard]] OrderSet BotOrdersFor(BotPolicy _policy, const Snapshot& _view, const MatchRules& _rules);

} // namespace Lockstep
