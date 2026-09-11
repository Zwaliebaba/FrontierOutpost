#pragma once

#include "Match.h"
#include "Melee.h"
#include "TickLog.h"

#include <span>
#include <vector>

namespace Lockstep
{

/// Everything the server hands the simulation for one tick.
///
/// Orders AND PRESENCE, because they are different facts. A player who logs in and changes nothing
/// is present and is not on their way to becoming a custodian; a player whose orders arrive from a
/// retry is not thereby present twice. The one-pager's custodian rule counts absence, and absence
/// is something only the server can observe -- the simulation must never ask a clock (R16,
/// ADR-018), so it is told.
struct TickInput
{
  std::span<const OrderSet> orders;
  /// Who the server saw since the last lock. Ignored when `presenceUnknown` is set.
  std::span<const PlayerId> present;

  /// When true, every player counts as present and `present` is not read.
  ///
  /// It is the default because in Stage A there is no server to report presence, and a caller with
  /// nothing to say about it means "this tick is not about absence" rather than "nobody was here".
  /// `4X-02`'s server always sets this false and fills `present`.
  bool presenceUnknown = true;
};

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
  /// `_input.orders` may be in any order and may be missing players. Duplicate sets for one player
  /// are refused; the first is kept, so a retried submission cannot double a build.
  [[nodiscard]] static Match Resolve(const Match& _before, const TickInput& _input, TickLog& _outLog);

  /// What a fight between these sides would do, without fighting it.
  ///
  /// The orders rail's "preview: 14 v 11 (+def) - 6 left". It runs the same `ResolveMelee` the
  /// resolver runs, which is the only reason it can be trusted: a preview computed by a second copy
  /// of the arithmetic is a preview that will one day disagree with the battle.
  [[nodiscard]] static std::vector<MeleeSide> Preview(const Match& _match, SystemId _system, std::span<const MeleeSide> _extra);

private:
  [[nodiscard]] static Match Lock(const Match& _in, const TickInput& _input, TickLog& _log);
  [[nodiscard]] static Match Produce(const Match& _in, TickLog& _log);
  [[nodiscard]] static Match Move(const Match& _in, TickLog& _log);
  [[nodiscard]] static Match Fight(const Match& _in, TickLog& _log);
  [[nodiscard]] static Match Claim(const Match& _in, TickLog& _log);
  [[nodiscard]] static Match Reckon(const Match& _in, TickLog& _log);
  static void WriteDigest(const Match& _in, TickLog& _log);
};

} // namespace Lockstep
