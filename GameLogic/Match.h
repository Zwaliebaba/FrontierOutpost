#pragma once

#include "Galaxy.h"
#include "MatchRules.h"
#include "Orders.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

/// What has changed about a system since the galaxy was generated.
///
/// Held here rather than on `GalaxySystem`, and that split is deliberate: the `Galaxy` is the
/// board and is never written after generation, so anything that reads topology reads something
/// that cannot have moved under it. `GalaxySystem::owner` is who STARTS there; this is who holds
/// it now.
struct SystemState
{
  PlayerId owner;
  bool hasShipyard = false;
  bool hasMiningStation = false;

  /// Who is partway through taking this system, and for how many consecutive ticks they have held
  /// it uncontested. The one-pager's siege rule: two consecutive ticks, and one contested tick
  /// resets it to nothing (*Tick resolution* §5).
  PlayerId siegeBy;
  std::uint32_t siegeTicks = 0;

  /// The tick it last changed hands, or 0. The map shows it.
  std::uint32_t capturedAt = 0;
};

/// A fleet, at a system or partway along a lane.
///
/// A fleet is never in two places: `at` is valid when it is parked and invalid while it is under
/// way, and `movingTo` is the reverse. That is checked by `Match::IsConsistent` rather than left
/// as a comment, because "which of these two fields means anything right now" is exactly the kind
/// of invariant that decays.
struct MatchFleet
{
  PlayerId owner;
  std::uint32_t ships = 0;

  SystemId at;
  SystemId movingFrom;
  SystemId movingTo;
  /// Ticks still to run on the current lane. Zero when parked.
  std::uint32_t ticksRemaining = 0;

  /// Whether this fleet reached where it is standing THIS tick.
  ///
  /// Combat's incumbency rule reads it, and reads nothing else: an incumbent is a fleet that was
  /// already here, not the system's owner. That distinction is the one-pager's -- "simultaneous
  /// arrivals at an empty system get none" -- and it would be lost if incumbency were derived from
  /// ownership, because at an empty system nobody owns anything.
  bool arrivedThisTick = false;

  /// The system this fleet left this tick, if it left one. Sub-phase 4a's rear-guard reads it.
  SystemId departedFrom;

  /// Where the lock told this fleet to go, consumed by the movement phase and cleared.
  ///
  /// It is STATE rather than a parameter threaded from phase 1 to phase 3, because that is what it
  /// is: between the lock and the move, this fleet has been told something and has not done it
  /// yet. Threading it would work equally well until the day a phase in between wants to know, and
  /// then it would be a second source of truth.
  SystemId orderedTo;

  /// A destroyed fleet is kept and marked rather than erased, so that every id in a `TickLog` or a
  /// digest still resolves after the fact. *Replay tick N* reads those logs.
  bool destroyed = false;

  [[nodiscard]] bool InTransit() const noexcept
  {
    return ticksRemaining > 0;
  }
};

/// An offer that is on the table.
struct OpenProposal
{
  ProposalId id;
  PlayerId from;
  PlayerId to;
  ProposalKind kind = ProposalKind::OpenLane;
  LaneId lane;
  std::uint32_t ticks = 0;
  /// A lane to open as well, if this is accepted (`ProposalOrder::conditionalLane`).
  LaneId conditionalLane;
  /// The tick it was made. It closes `proposalWindowTicks` after this.
  std::uint32_t openedAt = 0;
};

/// A trade lane that is open and paying.
struct ActiveTradeLane
{
  LaneId lane;
  PlayerId a;
  PlayerId b;
  std::uint32_t openedAt = 0;
};

/// An accepted proposal that is not a trade lane.
///
/// The one-pager offers three kinds and only one of them is a mechanic: a trade lane is a building
/// with two owners and pays. The other two are RECORDED AND NOT ENFORCED, which is the design
/// speaking -- "no enforced treaties" is the whole reason the trade lane is called *the one
/// consensual mechanic*. Sharing scouting lifts fog (Step 8); holding fire does nothing at all
/// except make a breach reportable, which is the only sanction the game has.
struct Agreement
{
  AgreementKind kind = AgreementKind::ShareScouting;
  PlayerId a;
  PlayerId b;
  std::uint32_t openedAt = 0;
  /// The tick it lapses. Zero means it does not -- shared scouting runs until somebody stops it.
  std::uint32_t expiresAt = 0;

  [[nodiscard]] bool Covers(PlayerId _first, PlayerId _second) const noexcept
  {
    return (a == _first && b == _second) || (a == _second && b == _first);
  }
};

/// A pair of players who have met.
///
/// Kept so that first contact is raised once and not every tick two empires remain adjacent. The
/// one-pager's prompt -- "Contact: [player]. Propose trade lane?" -- is a one-off, and an offer to
/// open a lane that arrives every six hours forever is a notification stream, which this game does
/// not have.
struct Contact
{
  /// Always the lower player id, so a pair has one representation.
  PlayerId a;
  PlayerId b;
  std::uint32_t tick = 0;
};

struct PlayerState
{
  std::uint32_t credits = 0;
  std::uint32_t score = 0;
  /// The last tick this player submitted orders. The custodian rule counts from it -- and it is
  /// TOLD to the simulation rather than read off a clock, because R16 forbids a clock in here and
  /// ADR-018 says why.
  std::uint32_t lastActiveTick = 0;
  bool conceded = false;
};

/// The authoritative state of one match.
///
/// It is a value: copyable, comparable by hash, with no pointers into itself and nothing shared.
/// That is what makes `TickResolver` able to take one and return the next rather than mutate in
/// place, and it is what makes "resolve the same tick twice and get the same state" a test that
/// can actually be written.
class Match
{
public:
  /// Generates the galaxy and puts everyone on their capital with a fleet and some credits.
  ///
  /// Fatal on rules that contradict themselves -- a trade lane that pays no more than an internal
  /// one, a player count the generator refuses. Those are not states to recover from; they are a
  /// caller that has misconfigured the game.
  [[nodiscard]] static Match Create(const MatchRules& _rules, std::uint64_t _seed);

  [[nodiscard]] const Galaxy& GalaxyGraph() const noexcept
  {
    return m_galaxy;
  }
  [[nodiscard]] const MatchRules& Rules() const noexcept
  {
    return m_rules;
  }
  [[nodiscard]] std::uint64_t Seed() const noexcept
  {
    return m_seed;
  }
  [[nodiscard]] std::uint32_t Tick() const noexcept
  {
    return m_tick;
  }
  [[nodiscard]] bool IsOver() const noexcept
  {
    return m_tick >= m_rules.matchLengthTicks;
  }

  [[nodiscard]] const std::vector<SystemState>& Systems() const noexcept
  {
    return m_systems;
  }
  [[nodiscard]] const std::vector<MatchFleet>& Fleets() const noexcept
  {
    return m_fleets;
  }
  [[nodiscard]] const std::vector<PlayerState>& Players() const noexcept
  {
    return m_players;
  }
  [[nodiscard]] const std::vector<OpenProposal>& Proposals() const noexcept
  {
    return m_proposals;
  }
  [[nodiscard]] const std::vector<ActiveTradeLane>& TradeLanes() const noexcept
  {
    return m_tradeLanes;
  }
  [[nodiscard]] const std::vector<Agreement>& Agreements() const noexcept
  {
    return m_agreements;
  }
  [[nodiscard]] const std::vector<Contact>& Contacts() const noexcept
  {
    return m_contacts;
  }

  [[nodiscard]] const SystemState& SystemAt(SystemId _system) const
  {
    return m_systems[_system.AsSize()];
  }
  [[nodiscard]] const MatchFleet& FleetAt(FleetId _fleet) const
  {
    return m_fleets[_fleet.AsSize()];
  }
  [[nodiscard]] const PlayerState& PlayerAt(PlayerId _player) const
  {
    return m_players[_player.AsSize()];
  }

  [[nodiscard]] bool HasPlayer(PlayerId _player) const noexcept
  {
    return _player.IsValid() && _player.AsSize() < m_players.size();
  }
  [[nodiscard]] bool HasFleet(FleetId _fleet) const noexcept
  {
    return _fleet.IsValid() && _fleet.AsSize() < m_fleets.size();
  }
  [[nodiscard]] bool HasSystem(SystemId _system) const noexcept
  {
    return _system.IsValid() && _system.AsSize() < m_systems.size();
  }

  /// The open proposal with this id, or null. Proposals are removed when they close, so this is a
  /// search rather than an index -- the list is at most a handful long.
  [[nodiscard]] const OpenProposal* FindProposal(ProposalId _proposal) const;

  /// Whether a trade lane is open on this lane.
  [[nodiscard]] bool IsTradeLane(LaneId _lane) const;

  /// The open trade lane on this lane, or null.
  [[nodiscard]] const ActiveTradeLane* FindTradeLane(LaneId _lane) const;

  /// Whether an agreement of this kind is in force between two players.
  [[nodiscard]] bool HasAgreement(AgreementKind _kind, PlayerId _first, PlayerId _second) const;

  /// Whether these two have met. First contact is raised once (Contact).
  [[nodiscard]] bool HaveMet(PlayerId _first, PlayerId _second) const;

  /// Every rejection in `_orders`, in the order the orders appear.
  ///
  /// Reads the match and nothing else; changes nothing. That separation is what lets the client be
  /// shown a refusal *before* the lock and the resolver apply the same rule *at* the lock, without
  /// two copies of the rule.
  [[nodiscard]] std::vector<RejectedOrder> Validate(const OrderSet& _orders) const;

  /// Whether a capital may be attacked yet (one-pager, *Pacing devices*: twelve ticks, with a
  /// visible countdown).
  [[nodiscard]] bool IsCapitalGuarded(SystemId _system) const;

  /// A hash of the whole state. Two matches with the same hash are the same match.
  ///
  /// This is the determinism test's instrument, and it is why every field a rule reads has to be
  /// in here: a value the resolver uses but the hash ignores is a value that can drift between two
  /// machines without any test noticing (ADR-018).
  [[nodiscard]] std::uint64_t Hash() const;

  /// Checks the invariants this class claims: a fleet is parked or under way and never both, every
  /// id in range, siege state paired with a besieger. Debug-time assurance for the resolver, which
  /// is the only thing that writes any of it.
  [[nodiscard]] bool IsConsistent() const;

  // ---- Mutation, for the resolver ---------------------------------------------------------------
  //
  // `TickResolver` is the only intended caller. These are public rather than friended because a
  // friend list that grows is a class that has stopped having an interface -- and because the
  // tests build states directly to put the resolver in a corner it would take twenty ticks to
  // reach by playing.

  [[nodiscard]] std::vector<SystemState>& MutableSystems() noexcept
  {
    return m_systems;
  }
  [[nodiscard]] std::vector<MatchFleet>& MutableFleets() noexcept
  {
    return m_fleets;
  }
  [[nodiscard]] std::vector<PlayerState>& MutablePlayers() noexcept
  {
    return m_players;
  }
  [[nodiscard]] std::vector<OpenProposal>& MutableProposals() noexcept
  {
    return m_proposals;
  }
  [[nodiscard]] std::vector<ActiveTradeLane>& MutableTradeLanes() noexcept
  {
    return m_tradeLanes;
  }
  [[nodiscard]] std::vector<Agreement>& MutableAgreements() noexcept
  {
    return m_agreements;
  }
  [[nodiscard]] std::vector<Contact>& MutableContacts() noexcept
  {
    return m_contacts;
  }

  void SetTick(std::uint32_t _tick) noexcept
  {
    m_tick = _tick;
  }

  [[nodiscard]] FleetId AddFleet(MatchFleet _fleet);

  /// The next proposal id, which only ever goes up. Ids are never reused, so a digest entry from
  /// tick 12 still means what it said at tick 60.
  [[nodiscard]] ProposalId TakeNextProposalId() noexcept;

private:
  Galaxy m_galaxy;
  MatchRules m_rules;
  std::uint64_t m_seed = 0;
  std::uint32_t m_tick = 0;

  std::vector<SystemState> m_systems;
  std::vector<MatchFleet> m_fleets;
  std::vector<PlayerState> m_players;
  std::vector<OpenProposal> m_proposals;
  std::vector<ActiveTradeLane> m_tradeLanes;
  std::vector<Agreement> m_agreements;
  std::vector<Contact> m_contacts;

  std::int32_t m_nextProposalId = 0;
};

} // namespace Frontier
