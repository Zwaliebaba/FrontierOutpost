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

  // ---- Starting position -----------------------------------------------------------------------

  /// Ships in the fleet each player starts with, parked at their capital. A player with no fleet
  /// has no move to make on tick one, and the first thing the loop has to be able to test is a
  /// fleet moving.
  std::uint32_t startingShips = 10;

  /// Credits each player starts with. Enough for one building, so the first lock is a real choice
  /// rather than a wait.
  std::uint32_t startingCredits = 20;

  // ---- Production (resolution phase 2) ---------------------------------------------------------
  //
  // Initial values. Phase 0 of the test plan exists to change them, and nothing below is derived
  // from anything else, so they can be changed one at a time.

  /// Credits a held system produces each tick.
  std::uint32_t creditsPerSystem = 2;

  /// What a capital adds on top. A capital is worth holding beyond the guard window.
  std::uint32_t capitalCreditsBonus = 4;

  /// What a mining station adds to the system it is on.
  std::uint32_t miningStationCredits = 4;

  /// Ships a shipyard adds to the fleet at its system each tick.
  std::uint32_t shipsPerShipyard = 2;

  /// What a lane between two systems the same player holds pays that player each tick.
  std::uint32_t internalLaneIncome = 1;

  /// What an open trade lane pays EACH of its two owners each tick.
  ///
  /// THE ONE-PAGER MAKES THIS A STRICT INEQUALITY: a trade lane "pays more than any internal
  /// lane", and that gap is the entire incentive to talk to a neighbour rather than expand into
  /// them. `Match::Create` refuses rules where it does not hold, because a build a rational player
  /// would never make is a mechanic that has quietly ceased to exist.
  std::uint32_t tradeLaneIncome = 6;

  // ---- Combat (resolution phase 4) ---------------------------------------------------------------
  //
  // ADR-021 argues these. They are INITIAL VALUES and Phase 0 exists to change them; what is not
  // negotiable is the shape, which the one-pager fixes: integer, proportional spread, fixed rounds,
  // deterministic, a tie is mutual attrition.

  /// How many rounds one melee runs. Fixed, so a fight cannot run long because it is close.
  std::uint32_t combatRounds = 3;

  /// What fraction of its effective strength a side deals each round, as a percentage.
  std::uint32_t damagePercentPerRound = 50;

  /// The incumbent's multiplier, as a percentage. An incumbent is a fleet that was already at the
  /// system -- not the system's owner, which is why simultaneous arrivals at an empty system get
  /// nothing.
  std::uint32_t defenderBonusPercent = 125;

  /// Sub-phase 4a, the rear-guard: a fleet that leaves a system a hostile arrives at takes one free
  /// round from the arrivals.
  ///
  /// **Off, and built anyway.** The one-pager is explicit: "switched off until Phase 0 shows
  /// dancing dominates; if enabled, dancing stays possible and stops being free." A switch that has
  /// never been on is a switch that does not work, so it is implemented and tested now and defaults
  /// to false.
  bool rearGuardEnabled = false;

  // ---- Build costs (order validation, step 3) --------------------------------------------------

  std::uint32_t shipyardCost = 20;
  std::uint32_t miningStationCost = 15;

  /// Paid by the player who proposes the lane, at the lock the partner accepts it.
  std::uint32_t tradeLaneCost = 10;

  // ---- Players, score and the ending (step 7) ---------------------------------------------------

  /// What a held system is worth each tick, and what a capital adds.
  ///
  /// Score is recomputed from scratch every tick from what is held (one-pager: "public score, the
  /// leader is always visible"), so these are weights rather than a running total -- a player who
  /// loses half their empire drops, which is what makes the leader attackable.
  std::uint32_t scorePerSystem = 10;
  std::uint32_t capitalScoreBonus = 25;

  /// How much a custodian's garrisons weaken per absent tick, as a percentage of what is left.
  ///
  /// The one-pager's reason is social rather than mechanical: "their garrisons weaken with each
  /// tick of absence, so the territory is a public race among every neighbour who can reach it,
  /// not a private farm."
  std::uint32_t garrisonDecayPercent = 20;

  /// What a system conquered from a custodian yields, as a percentage, for the rest of the match
  /// whoever holds it. "The dropout's infrastructure decays under new ownership."
  std::uint32_t custodianSpoilsYieldPercent = 50;

  /// How long the first week is, in ticks. A player who becomes a custodian inside it scores
  /// nothing for the match -- "the only cost that reaches someone who has already stopped playing".
  std::uint32_t firstWeekTicks = 28;

  /// The share of all score on the board one player must hold to be dominant, as a percentage,
  /// and how many consecutive ticks they must hold it for the match to end early.
  ///
  /// "An early dominance threshold ends the match only if held for several consecutive ticks, so
  /// the leader stays attackable."
  std::uint32_t dominanceSharePercent = 60;
  std::uint32_t dominanceHoldTicks = 4;

  // ---- Visibility (step 8) -----------------------------------------------------------------------

  /// How many lanes out from something you hold or occupy you can see. One: your own systems and
  /// their immediate neighbours (ADR-022).
  std::uint32_t scoutingRangeLanes = 1;

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

/// A way a rules struct contradicts the game it is rules for.
///
/// These are not tuning mistakes -- Phase 0 is allowed to make a lane pay badly or a match run
/// short. They are settings under which a MECHANIC CEASES TO EXIST: a trade lane nobody would ever
/// build, a siege that captures instantly, a match with no ticks in it. Each would show up in
/// playtesting as "nobody used X" and take a week to trace back to a number.
enum class RulesProblem : std::uint8_t
{
  None,
  /// Outside the 6-12 the design is drawn for.
  PlayerCountOutOfRange,
  /// A trade lane that pays no more than an internal one. The one-pager makes the gap the entire
  /// incentive to talk to a neighbour rather than expand into them.
  TradeLaneNotWorthBuilding,
  /// A siege of zero ticks, which is a capture with no warning and no chance to answer it. The
  /// one-pager's siege rule exists so that losing a system is something you saw coming.
  SiegeIsInstant,
  /// A match with no ticks to play.
  NoTicksToPlay,
  /// A proposal window of zero, which closes every offer before anyone could answer.
  NoProposalWindow,
  /// Combat that runs no rounds, or deals no damage. Either makes every fight a draw and every
  /// fleet immortal, which removes the game rather than tuning it.
  CombatDecidesNothing,
  /// A dominance threshold at or below an even share, which one player would reach by playing
  /// normally, or above 100, which nobody could ever reach.
  DominanceUnreachableOrTrivial,
  /// A dominance hold of no ticks, which ends the match the instant somebody leads.
  DominanceNeedsNoHolding,
  /// A defender bonus below 100%, which would make holding a system worse than arriving at it and
  /// invert the one-pager's incumbency rule.
  DefenderBonusPunishesTheDefender
};

[[nodiscard]] constexpr const char* Describe(RulesProblem _problem) noexcept
{
  switch (_problem)
  {
  case RulesProblem::None:
    return "playable";
  case RulesProblem::PlayerCountOutOfRange:
    return "player count is outside the 6-12 the design is drawn for";
  case RulesProblem::TradeLaneNotWorthBuilding:
    return "a trade lane must pay more than an internal lane, or nobody would ever build one";
  case RulesProblem::SiegeIsInstant:
    return "a siege of no ticks is a capture nobody saw coming";
  case RulesProblem::NoTicksToPlay:
    return "a match needs at least one tick";
  case RulesProblem::NoProposalWindow:
    return "a proposal window of no ticks closes every offer before it can be answered";
  case RulesProblem::CombatDecidesNothing:
    return "combat that runs no rounds or deals no damage makes every fleet immortal";
  case RulesProblem::DominanceUnreachableOrTrivial:
    return "a dominance share must be above an even split and no more than the whole board";
  case RulesProblem::DominanceNeedsNoHolding:
    return "dominance held for no ticks ends the match the instant somebody leads";
  case RulesProblem::DefenderBonusPunishesTheDefender:
    return "a defender bonus below one hundred percent makes holding a system worse than arriving at it";
  default:
    return "unknown";
  }
}

/// Whether these rules describe a game that can be played.
///
/// Asked by `Match::Create`, which is fatal on a failure -- a caller that misconfigured the game
/// is a defect, not a state to recover from. It is a separate function rather than a check buried
/// in `Create` so that the server can ask BEFORE it has built anything (4X-02), and so that the
/// answer is testable without provoking a fatal.
[[nodiscard]] constexpr RulesProblem Check(const MatchRules& _rules) noexcept
{
  if (_rules.playerCount < MINIMUM_PLAYERS || _rules.playerCount > MAXIMUM_PLAYERS)
  {
    return RulesProblem::PlayerCountOutOfRange;
  }
  if (_rules.tradeLaneIncome <= _rules.internalLaneIncome)
  {
    return RulesProblem::TradeLaneNotWorthBuilding;
  }
  if (_rules.siegeTicks == 0)
  {
    return RulesProblem::SiegeIsInstant;
  }
  if (_rules.matchLengthTicks == 0)
  {
    return RulesProblem::NoTicksToPlay;
  }
  if (_rules.proposalWindowTicks == 0)
  {
    return RulesProblem::NoProposalWindow;
  }
  if (_rules.combatRounds == 0 || _rules.damagePercentPerRound == 0)
  {
    return RulesProblem::CombatDecidesNothing;
  }
  if (_rules.defenderBonusPercent < 100)
  {
    return RulesProblem::DefenderBonusPunishesTheDefender;
  }
  if (_rules.dominanceSharePercent <= (100U / _rules.playerCount) || _rules.dominanceSharePercent > 100U)
  {
    return RulesProblem::DominanceUnreachableOrTrivial;
  }
  if (_rules.dominanceHoldTicks == 0)
  {
    return RulesProblem::DominanceNeedsNoHolding;
  }
  return RulesProblem::None;
}

} // namespace Frontier
