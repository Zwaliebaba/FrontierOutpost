#pragma once

#include <cstdint>

namespace Frontier
{

/// Every number the game is played with, in one place.
///
/// The one-pager gives shapes and only occasionally numbers, and the test plan's whole Phase 0 is
/// "find broken mechanics, tune capital guard length, trade lane yield, lane costs, dominance
/// threshold". So these are DATA from the first line rather than constants that Phase 0 would have
/// to go hunting through the source for. A rule with a number in it that is not here is a rule
/// Phase 0 cannot tune.
///
/// R8: a public aggregate, so plain fields and brace initialization. R3: it is not a constant, so
/// the fields are camelCase and only the defaults below are UPPER_CASE.
///
/// THIS STRUCT GROWS WITH THE PLAN. It currently holds what generation needs plus the four numbers
/// the one-pager states outright. Combat parameters arrive with combat (4X-01 step 5), lane yield
/// with trade lanes (step 6), the dominance threshold with scoring (step 7). Adding them before
/// then would be a struct full of fields nothing reads, which is worse than a struct that grows.
struct MatchRules
{
  // ---- The match ----------------------------------------------------------------------------

  /// 6-8 for the prototype, up to 12 by design (one-pager, "Shape of a game"). The generator is
  /// sized by this and rejects anything outside it.
  std::uint32_t playerCount = 6;

  /// How long a match runs. Three weeks at four ticks a day is 84; Phase 0 runs a 48-hour match on
  /// a one-hour tick, which is 48.
  std::uint32_t matchLengthTicks = 84;

  /// Seconds between locks. Four a day in production, one hour in Phase 0, six in Phase 1 -- which
  /// is why it is a number here rather than a constant anywhere. Nothing in `GameLogic` reads it;
  /// it belongs to the match and the server schedules from it (4X-02).
  std::uint32_t tickIntervalSeconds = 6 * 60 * 60;

  // ---- Numbers the one-pager states outright --------------------------------------------------

  /// Capitals cannot be attacked for this many ticks. An explicit rule with a visible countdown,
  /// layered on top of the siege rule rather than derived from it (one-pager, "Pacing devices").
  std::uint32_t capitalGuardTicks = 12;

  /// How long a proposal stays open. Four ticks, "so every player sees it in at least one daily
  /// session" (one-pager, decision three).
  std::uint32_t proposalWindowTicks = 4;

  /// Ticks of absence before a player becomes a custodian. Reversible -- log in and resume.
  std::uint32_t custodianAbsenceTicks = 3;

  /// Consecutive uncontested ticks needed to take an owned system: siege, then capture.
  std::uint32_t siegeTicks = 2;

  // ---- Generation ------------------------------------------------------------------------------
  //
  // The constraints themselves are the one-pager's and are not negotiable here: each capital has a
  // rival capital within three ticks, one-tick lanes inside starting clusters, two-to-four-tick
  // lanes toward the frontier. What is tunable is how much galaxy there is.

  /// Systems in a player's starting cluster besides the capital itself, joined to it by one-tick
  /// lanes. Two makes a cluster with something to lose; one makes a capital with a suburb.
  std::uint32_t satellitesPerCapital = 2;

  /// Systems in the contested middle, per player. The frontier is where the two-to-four-tick lanes
  /// are and where the sealed region sits, so this is the number that decides how far apart the
  /// empires really are.
  std::uint32_t frontierSystemsPerPlayer = 1;

  /// The shortest path in ticks that must exist between some pair of capitals. "The generator
  /// guarantees each capital a rival capital within three ticks" (one-pager).
  std::uint32_t maximumTicksToNearestRival = 3;

  /// The cost band for a lane that leaves a starting cluster. "Two- to four-tick lanes toward the
  /// frontier."
  std::uint32_t frontierLaneMinimumTicks = 2;
  std::uint32_t frontierLaneMaximumTicks = 4;

  /// How many seeds the generator may reject before it gives up and reports that the RULES cannot
  /// be satisfied rather than that this seed could not. A generator that needs more than a handful
  /// of attempts is a generator with a bug, not a run of bad luck (GalaxyGenerator.h).
  std::uint32_t maximumSeedAttempts = 64;

  // ---- The sealed region -------------------------------------------------------------------
  //
  // Placed and drawn by 4X-01; nothing happens when it opens until Phase 2 (one-pager, "Build
  // order").

  /// The tick the region opens. Mid-match: "visible from tick one, opens at a known tick".
  std::uint32_t regionOpensAtTick = 60;

  /// Settlement sites inside it. Three, because "the region has several" and a race between six
  /// players over one site is not a race.
  std::uint32_t regionSiteCount = 3;
};

/// The bounds the one-pager fixes on player count. Outside these the generator refuses rather than
/// producing a galaxy nobody designed for.
inline constexpr std::uint32_t MINIMUM_PLAYERS = 6;
inline constexpr std::uint32_t MAXIMUM_PLAYERS = 12;

} // namespace Frontier
