#pragma once

#include "Galaxy.h"
#include "Orders.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Lockstep
{

/// The six phases, in the order the one-pager fixes them.
///
/// Combat is here and does nothing in Stage A up to step 5. It is in the enum rather than added
/// later because the ORDER is the rule -- movement before combat is what makes leaving beat
/// arriving -- and an order with a hole in it invites the hole being filled in the wrong place.
enum class Phase : std::uint8_t
{
  Lock,
  Production,
  Movement,
  Combat,
  Claims,
  Digest
};

[[nodiscard]] const char* Describe(Phase _phase) noexcept;

/// What a digest event is about.
///
/// Richer than the client's `EventKind`, which has seven values because it selects a dot colour.
/// This one names what happened, and the client seam maps many of these onto one of those. Doing
/// it the other way round -- making the simulation speak in dot colours -- is how a rule ends up
/// unable to say something because there was no colour for it.
enum class DigestKind : std::uint8_t
{
  Contact,
  ProposalReceived,
  ProposalAnswered,
  ProposalWithdrawn,
  ProposalIgnored,
  ProposalVoided,
  OrderRejected,
  SystemClaimed,
  SystemLost,
  SiegeBegun,
  Battle,
  LaneOpened,
  LaneCanceled,
  AgreementOpened,
  AgreementBreached,
  Economy,
  Region,
  Custodian,
  MatchEnded
};

[[nodiscard]] const char* Describe(DigestKind _kind) noexcept;

/// How much this event should change what the player does next.
///
/// ADR-020: the digest is sorted by a number each event carries, not by its kind. The values are
/// spread out so a later rule can land between two of them without renumbering everything.
namespace Severity
{
/// The match is over. Nothing that happened this tick matters more than that.
inline constexpr std::uint32_t MATCH_ENDED = 1000;
inline constexpr std::uint32_t LOST_A_SYSTEM = 900;
inline constexpr std::uint32_t BATTLE = 850;
inline constexpr std::uint32_t FIRST_CONTACT = 800;
inline constexpr std::uint32_t UNDER_SIEGE = 750;
/// A hold-fire agreement broken. It ranks with a proposal arriving rather than with a battle: the
/// battle itself is already reported, and this is the part that changes whom you trust.
inline constexpr std::uint32_t AGREEMENT_BREACHED = 700;
inline constexpr std::uint32_t PROPOSAL_ARRIVED = 600;
inline constexpr std::uint32_t PROPOSAL_RESOLVED = 500;
inline constexpr std::uint32_t TOOK_A_SYSTEM = 450;
inline constexpr std::uint32_t LANE_CHANGED = 400;
inline constexpr std::uint32_t AGREEMENT_MADE = 380;
inline constexpr std::uint32_t ORDER_REFUSED = 350;
inline constexpr std::uint32_t REGION = 300;
inline constexpr std::uint32_t CUSTODIAN = 250;
inline constexpr std::uint32_t ECONOMY = 100;
} // namespace Severity

/// One line of one player's digest.
///
/// `title` and `detail` are written here, in the simulation, because the one-pager says the digest
/// is prose the server writes rather than a template the client fills in. A client that assembled
/// these from ids would have to know every rule to phrase them, which is the whole thing the seam
/// exists to prevent.
struct DigestEntry
{
  DigestKind kind = DigestKind::Economy;
  std::uint32_t severity = Severity::ECONOMY;
  std::string title;
  std::string detail;

  /// What to focus when the player taps it. Any of these may be unset.
  SystemId system;
  LaneId lane;
  FleetId fleet;
  PlayerId other;
};

/// A fleet that was targeted: a hostile arrived, this tick, at the system it was standing on when
/// the tick began.
///
/// **This exists for the test plan's Phase 0 watch item**, which is quoted here because the reason
/// it must be recorded unconditionally is not obvious:
///
/// > log every departure that coincides with a hostile arrival at the same system. If a fleet
/// > escapes this way more than a third of the time it is targeted, and the dodging player retains
/// > or retakes the system, enable the rear-guard round and re-run.
///
/// That fraction is what decides whether `MatchRules::rearGuardEnabled` gets turned on. It was
/// briefly computed only *inside* the rear-guard branch, which made it uncollectable while the rear
/// guard was off -- the one number that decides whether to enable the mechanic, gated behind the
/// mechanic being enabled. It is now computed every tick regardless.
///
/// `dodged` is the numerator and the whole list is the denominator: a fleet that stayed and fought
/// was targeted too. The second half of the watch item -- whether the dodger *retains or retakes*
/// the system -- spans later ticks and is the reader's to work out; the simulation reports what
/// happened in this one.
struct Interception
{
  SystemId system;
  FleetId fleet;
  PlayerId defender;
  /// The hostile that arrived. The lowest player id when several did, so the record is the same on
  /// every machine.
  PlayerId arrival;
  /// Whether the fleet left rather than staying to fight. This is the dance.
  bool dodged = false;
  /// Whether sub-phase 4a actually cost the dodger anything, which it only does when the rear guard
  /// is switched on. False on every dodge while it is off, which is the state Phase 0 measures.
  bool rearGuardFired = false;
};

/// What one phase did, as prose, for *Replay tick N*.
struct PhaseRecord
{
  Phase phase = Phase::Lock;
  std::vector<std::string> lines;
};

/// Everything that happened in one tick.
///
/// It is the resolver's second output and it is not optional: the client's *Replay tick N* reads
/// it, and so does every test in step 4, which asserts against what the log says happened rather
/// than only against the state that came out. A rule that fires correctly but silently is a rule
/// nobody can debug at four ticks a day.
struct TickLog
{
  /// The tick that was resolved -- the state's tick BEFORE resolution, since resolving tick 46
  /// produces tick 47.
  std::uint32_t tick = 0;
  std::vector<PhaseRecord> phases;
  /// One list per player, indexed by player id.
  std::vector<std::vector<DigestEntry>> digests;

  /// Every fleet a hostile arrived on top of this tick, and whether it stayed. The test plan's
  /// defender-dancing watch item is a fraction of this list (Interception).
  std::vector<Interception> interceptions;

  /// How many of `interceptions` were dodges. The watch item's threshold is a third.
  [[nodiscard]] std::uint32_t Dodges() const;

  [[nodiscard]] const PhaseRecord* Find(Phase _phase) const;

  /// Every line of every phase, joined. Tests read this when they care that something happened
  /// rather than in which phase.
  [[nodiscard]] std::vector<std::string> AllLines() const;
};

} // namespace Lockstep
