#pragma once

#include <array>
#include <cstdint>

namespace Frontier
{

/// Every tunable the match runs on, as data rather than as constants (ADR-004).
///
/// This is data on purpose. The one-pager quantifies almost none of it, and the test plan's Phase 0
/// exists to tune exactly these numbers -- "run as many as needed ... tune capital guard length,
/// trade lane yield, lane costs, dominance threshold". A number compiled into the resolver is a
/// number that needs a build to change, which is the wrong shape for a value whose whole purpose is
/// to be argued with over a week of playtests.
///
/// R8: a public aggregate, so plain fields. R6: every one carries its unit.
///
/// Fields for rules MVP-02 does not build yet are here anyway when an ADR requires them to exist --
/// the rear-guard round and the supply cost are switches that are off, not features that are absent,
/// and having them here is what makes turning one on a data change.
struct Rules
{
  // ---- The galaxy the generator emits (ADR-014) ----

  /// Upper bound, in map units, on the drawn length of a lane costing 1, 2, 3 and 4 ticks. A pair
  /// of systems further apart than the last entry gets no lane at all.
  ///
  /// This array is the whole of ADR-014's monotonicity guarantee: the generator assigns a lane's
  /// cost FROM its drawn length by looking it up here, so a longer lane can never cost less. The
  /// alternative -- pick a cost, then hope the placement agrees -- is what makes a map that lies.
  std::array<std::int32_t, 4> laneCostBandUpperUnits;

  /// The capital-to-adjacent-capital chord. Sampled in this range and kept inside band 3, which is
  /// what delivers the one-pager's guarantee of a rival capital within three ticks: the lane is
  /// direct, so the shortest path is the lane.
  std::int32_t capitalChordMinUnits;
  std::int32_t capitalChordMaxUnits;

  /// Outer ring (the capitals) to the frontier ring, radially.
  std::int32_t frontierRadialGapUnits;

  /// Frontier ring to the sealed ring, radially. Sampled.
  std::int32_t sealedInnerGapMinUnits;
  std::int32_t sealedInnerGapMaxUnits;

  /// The sealed ring's radius is clamped into this range whatever the gap works out to: too small
  /// and its own lanes fall into band 1, too large and they run past band 4.
  std::int32_t sealedRadiusMinUnits;
  std::int32_t sealedRadiusMaxUnits;
  std::int32_t sealedSystemCount;

  /// A starting cluster: satellites on an outward arc around the capital, this far from it, this
  /// many of them, this far apart in angle.
  std::int32_t satelliteRadiusUnits;
  std::int32_t satelliteMinCount;
  std::int32_t satelliteMaxCount;
  std::int32_t satelliteSpreadTurns16;

  /// No two systems closer than this. It is what stops two meshes drawing on top of each other.
  std::int32_t minSeparationUnits;

  /// Every system's placement is nudged by up to this much on each axis, so two matches of the same
  /// seat count are not the same galaxy. Small on purpose: the margins every constraint above is
  /// built with are a few units, and jitter is what spends them.
  std::int32_t positionJitterUnits;

  /// The generator rejects a seed whose capitals are not this close to a rival.
  std::int32_t capitalRivalMaxTicks;

  /// Yield per tick by system kind. The frontier and the region are richer than home, which is what
  /// makes the one-pager's first interesting decision -- near and safe against far and rich -- a
  /// decision rather than arithmetic.
  std::int32_t capitalYieldPerTick;
  std::int32_t clusterYieldMinPerTick;
  std::int32_t clusterYieldMaxPerTick;
  std::int32_t frontierYieldMinPerTick;
  std::int32_t frontierYieldMaxPerTick;
  std::int32_t sealedYieldMinPerTick;
  std::int32_t sealedYieldMaxPerTick;

  // ---- The match ----

  std::int32_t startingIncome;
  /// The pinned fleet a capital starts with (ADR-018).
  std::int32_t startingGarrisonStrength;
  /// The one-pager's capital guard: a capital cannot be attacked for this many ticks, as an explicit
  /// rule with a visible countdown rather than something derived from the siege rule.
  std::uint64_t capitalGuardTicks;
  /// A three-week match at four ticks a day. The end date is known when the match starts.
  std::uint64_t matchLengthTicks;
  /// The sealed region is visible from tick one and opens here. Nothing in MVP-02 can enter it.
  std::uint64_t sealedOpensTick;

  // ---- Switches that are off ----

  /// ADR-004 sub-phase 4a. Off until Phase 0 shows that defender dancing dominates.
  bool rearGuardRound;
  /// The one-pager names supply cost once, as anti-snowball beside distance, and never defines it.
  /// Zero: lane cost does that work in MVP-02, and Phase 0 can switch this on without a build.
  std::int32_t fleetUpkeepPerStrengthPerTick;
  /// ADR-017: how many lanes out a fleet observes. One, so a scout is worth sending along a chain.
  std::int32_t scoutingRevealLanes;
};

/// The starting values. Measured where a figure is quoted, chosen where it is not.
///
/// The galaxy geometry here was tuned against a prototype of this exact integer arithmetic and
/// measured on 2026-09-10 at 0 rejections in 1000 seeds at each of 6, 8 and 12 seats; the same
/// measurement runs in `GameLogicTests` against the real generator. Every gameplay number below is
/// a starting guess for Phase 0 to tune and is not a measurement of anything.
inline constexpr Rules DEFAULT_RULES = {
  .laneCostBandUpperUnits = {25, 55, 90, 140},
  .capitalChordMinUnits = 80,
  .capitalChordMaxUnits = 87,
  .frontierRadialGapUnits = 30,
  .sealedInnerGapMinUnits = 30,
  .sealedInnerGapMaxUnits = 36,
  .sealedRadiusMinUnits = 18,
  .sealedRadiusMaxUnits = 70,
  .sealedSystemCount = 3,
  .satelliteRadiusUnits = 20,
  .satelliteMinCount = 2,
  .satelliteMaxCount = 4,
  .satelliteSpreadTurns16 = 8010, // 44 degrees
  .minSeparationUnits = 10,
  .positionJitterUnits = 1,
  .capitalRivalMaxTicks = 3,
  .capitalYieldPerTick = 6,
  .clusterYieldMinPerTick = 2,
  .clusterYieldMaxPerTick = 4,
  .frontierYieldMinPerTick = 5,
  .frontierYieldMaxPerTick = 9,
  .sealedYieldMinPerTick = 8,
  .sealedYieldMaxPerTick = 12,
  .startingIncome = 20,
  .startingGarrisonStrength = 10,
  .capitalGuardTicks = 12,
  .matchLengthTicks = 84, // three weeks at four ticks a day
  .sealedOpensTick = 42,
  .rearGuardRound = false,
  .fleetUpkeepPerStrengthPerTick = 0,
  .scoutingRevealLanes = 1,
};

} // namespace Frontier
