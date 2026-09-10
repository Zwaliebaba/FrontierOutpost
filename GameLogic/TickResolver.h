#pragma once

#include "Match.h"
#include "TickLog.h"

#include <span>

namespace Frontier
{

/// Resolves one tick: state and orders in, the next state and a log out.
///
/// **The signature is the rule.** Every phase below is `Match(const Match&, ...)` -- it reads a
/// state it cannot write and returns the next one. That is the one-pager's *no order reads
/// another's write within a phase*, enforced by the type system rather than by everybody
/// remembering: within a phase, every read comes from the `const&` and every write goes to the
/// copy, so the tenth fleet processed sees exactly what the first one saw.
///
/// The plan says it outright -- "if the resolver's signature makes it possible to read what
/// another order wrote in the same phase, the signature is wrong" -- and the cost is five copies
/// of a `Match` per tick. At twelve players and sixty systems that is a few kilobytes, four times
/// a day. It buys a rule that cannot quietly stop being true.
///
/// It is a pure function (ADR-018): no clock, no allocation that outlives it, no global. Resolve
/// the same tick twice and the two results hash the same, which `GameLogicTests` asserts.
class TickResolver
{
public:
  /// `_orders` may be in any order and may be missing players -- a player who did not log in
  /// submits nothing, which is a normal tick and the thing the custodian rule counts. Duplicate
  /// sets for one player are refused; the first is kept, so a retried submission cannot double a
  /// build.
  [[nodiscard]] static Match Resolve(const Match& _before, std::span<const OrderSet> _orders, TickLog& _outLog);

private:
  [[nodiscard]] static Match Lock(const Match& _in, std::span<const OrderSet> _orders, TickLog& _log);
  [[nodiscard]] static Match Produce(const Match& _in, TickLog& _log);
  [[nodiscard]] static Match Move(const Match& _in, TickLog& _log);
  [[nodiscard]] static Match Fight(const Match& _in, TickLog& _log);
  [[nodiscard]] static Match Claim(const Match& _in, TickLog& _log);
  static void WriteDigest(const Match& _in, TickLog& _log);
};

} // namespace Frontier
