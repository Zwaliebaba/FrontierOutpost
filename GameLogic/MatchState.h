#pragma once

#include <cstdint>
#include <vector>

namespace Frontier
{

// The match, as a value. ADR-004 makes the resolver a pure function of this, the locked orders and
// the rules, so everything here is data and nothing here has behavior.
//
// Ids are indices. `state.systems[id].id == id` for every system, and the same for lanes, fleets
// and seats. That is asserted by the tests rather than merely intended, and it is what lets every
// walk over the state be an ordered walk over a vector -- R16 bans an unordered container in this
// library outright, because a resolver whose answer depends on a hash order is a resolver that
// disagrees with itself between builds.

using SystemId = std::uint16_t;
using LaneId = std::uint16_t;
using FleetId = std::uint16_t;
using SeatId = std::uint8_t;

inline constexpr SystemId NO_SYSTEM = 0xFFFF;
inline constexpr LaneId NO_LANE = 0xFFFF;
inline constexpr SeatId NO_SEAT = 0xFF;

/// What a system is for. The generator assigns this and the resolver reads it; it is not a
/// gameplay rule in itself.
enum class SystemKind : std::uint8_t
{
  /// A seat's starting system. Cannot be attacked for the first `capitalGuardTicks` (the one-pager's
  /// capital guard), and its fall is what sends a seat into Exile in a later slice.
  Capital,
  /// A system of a seat's starting cluster. One-tick lanes inside the cluster (ADR-014).
  Cluster,
  /// The frontier ring: further out in ticks, richer in yield. The one-pager's "far and rich".
  Frontier,
  /// A site inside the sealed region. Generated and visible from tick one; nothing may enter it
  /// until the region opens, which is not in MVP-02 at all.
  Sealed
};

/// A point on the map plane, in map units. Whole numbers, because ADR-014 has the generator emit
/// the coordinates and ADR-003 projects a lattice of them onto a lattice of pixels.
///
/// A map unit is a tabletop unit and not a metre (ADR-003): the diorama is not to scale.
struct MapPoint
{
  std::int32_t xUnits;
  std::int32_t yUnits;
};

struct System
{
  SystemId id;
  MapPoint position;
  SystemKind kind;
  /// The seat whose starting cluster this belongs to, or NO_SEAT. Fixed at generation and never
  /// changes -- it is what "conquered from a custodian" and "your home cluster" are asked about.
  SeatId homeSeat;
  /// Who holds it now, or NO_SEAT. Changes on capture.
  SeatId owner;
  std::int32_t yieldPerTick;
};

struct Lane
{
  LaneId id;
  SystemId endA;
  SystemId endB;
  /// Ticks to traverse. Authored at generation, never derived at run time, and monotone in the
  /// lane's drawn length so the map cannot lie about travel time (ADR-014).
  std::int32_t costTicks;
};

/// An owner, a place and an amount of force (ADR-018).
///
/// A fleet is divisible: `strength` is the single combat quantity, splitting makes two fleets, and
/// two fleets of one owner at one system coalesce at the end of a tick. A garrison is a fleet with
/// `pinned` set, which takes no move order but is an incumbent for the defender bonus and counts as
/// presence for claims and sieges exactly like any other fleet.
struct Fleet
{
  FleetId id;
  SeatId owner;
  /// The system it sits at, or NO_SYSTEM while in transit.
  SystemId atSystem;
  /// The lane it is on, or NO_LANE while at a system.
  LaneId onLane;
  /// Where it is headed while in transit, or NO_SYSTEM. Public once departed: the one-pager makes a
  /// fleet in transit visible to everyone, with its tick-ETA.
  SystemId towardSystem;
  std::uint64_t departedTick;
  std::uint64_t arrivesTick;
  std::int32_t strength;
  bool pinned;
};

/// What one seat remembers about one system (ADR-017).
///
/// The topology is public, so nothing here is about where a system is. This is its CONTENTS as of
/// the last tick this seat had a fleet in range, which is what makes a remembered system drawable
/// as "what I saw at tick N" rather than as a guess about now.
struct SystemMemory
{
  /// The tick the contents below were observed. Zero while `known` is false.
  std::uint64_t observedTick;
  std::int32_t yieldPerTick;
  std::int32_t garrisonStrength;
  SeatId owner;
  bool known;
};

struct Seat
{
  SeatId id;
  SystemId capital;
  std::int32_t income;
  /// Indexed by `SystemId`, one entry per system, sized by the generator. A vector rather than a
  /// set of the systems seen, because indexing is what makes the visibility filter a walk with no
  /// lookup in it -- and R16 has no unordered container available to it anyway.
  std::vector<SystemMemory> memory;
};

struct MatchState
{
  /// The seed this galaxy was generated from. In the state because a Phase 0 report of "my fleet
  /// vanished at tick 31" is only reproducible if the galaxy is (ADR-019).
  std::uint64_t seed;
  std::uint64_t tick;
  std::uint64_t endTick;
  std::uint64_t sealedOpensTick;
  /// All four ordered by id, and id is the index.
  std::vector<System> systems;
  std::vector<Lane> lanes;
  std::vector<Fleet> fleets;
  std::vector<Seat> seats;
};

/// Squared length of a lane, in map units squared.
///
/// Squared, and never a length. There is no square root anywhere in the generator or the resolver:
/// every question either asks actually is a comparison, comparison is monotone under squaring, and
/// an integer square root would be a rounding step with nothing to buy.
[[nodiscard]] std::int64_t LaneLengthSquaredUnits(const MatchState& _state, const Lane& _lane) noexcept;

[[nodiscard]] std::int64_t DistanceSquaredUnits(const MapPoint& _a, const MapPoint& _b) noexcept;

/// True when one lane joins the two systems.
[[nodiscard]] bool AreAdjacent(const MatchState& _state, SystemId _a, SystemId _b) noexcept;

/// Cost in ticks from `_source` to every system, by Dijkstra over lane costs. Unreachable systems
/// come back as -1.
///
/// The implementation is an O(V^2) scan over a vector rather than a heap, which for the sixty-odd
/// systems this generator emits is faster in practice and, more to the point, has no tie-breaking
/// to get wrong: a heap of equal keys pops in an order that is the container's business.
[[nodiscard]] std::vector<std::int32_t> ShortestPathTicksFrom(const MatchState& _state, SystemId _source);

} // namespace Frontier
