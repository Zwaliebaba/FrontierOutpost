#pragma once

#include "Trigonometry.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

// The wire records that cross a transport, and how they turn into bytes.
//
// A NOTE ON WHERE THIS LIVES. R9 says the engine knows nothing about this game, and `ShipState`
// is game vocabulary. It is in NeuronCore because MVP-01 step 5 puts it here, and because both
// halves of the process need it and NeuronCore is the only thing they share (AGENTS.md 2). It is
// worth knowing that these two records are the one place the engine has a game noun in it, and
// that generalizing them -- an entity state, a move order -- is a rename rather than a redesign
// on the day a second kind of thing needs to move.
//
// The records are serialized field by field, little-endian, rather than memcpy'd. A struct's
// layout is the compiler's business: padding, alignment and endianness are all things it is
// allowed to change and this format is not. Sizes are asserted at compile time so that adding a
// field is a build error somewhere rather than a wire mismatch somewhere else.

/// "Go here." The order a click becomes (MVP-01 step 6).
///
/// R8: a wire record is a public aggregate, so plain fields. R6: the unit is in the name, because
/// a simulation measured in millimetres and world units at once is a defect class rather than a
/// mistake.
struct MoveToOrder
{
  std::int64_t targetXMillimetres;
  std::int64_t targetZMillimetres;
};

/// What the server says about the ship each tick (ADR-005).
struct ShipState
{
  /// The tick this state was taken at. The client needs it to interpolate and to notice a gap;
  /// at 20 Hz a uint64 runs for 29 billion years.
  std::uint64_t tick;
  std::int64_t positionXMillimetres;
  std::int64_t positionZMillimetres;
  std::int32_t speedMillimetresPerTick;
  Turns16 headingTurns16;
  /// Padding, and named as such so that the record is a fixed 32 bytes rather than whatever the
  /// compiler chose. Reserved for a flag byte the MVP does not have yet.
  std::uint16_t reserved;
};

inline constexpr std::size_t MOVE_TO_ORDER_BYTES = 16;
inline constexpr std::size_t SHIP_STATE_BYTES = 32;

void Serialize(const MoveToOrder& _order, std::span<std::byte, MOVE_TO_ORDER_BYTES> _bytes) noexcept;
[[nodiscard]] MoveToOrder DeserializeMoveToOrder(std::span<const std::byte, MOVE_TO_ORDER_BYTES> _bytes) noexcept;

void Serialize(const ShipState& _state, std::span<std::byte, SHIP_STATE_BYTES> _bytes) noexcept;
[[nodiscard]] ShipState DeserializeShipState(std::span<const std::byte, SHIP_STATE_BYTES> _bytes) noexcept;

// ---------------------------------------------------------------------------------------------
// The 4X protocol (ADR-005, ADR-006).
//
// Everything above this line is the MVP-01 ship path and is deleted by slice 2b of
// `Design/Plans/MVP-02-TheLoop.md`. It is still here because slice 2a adds the records below
// without moving the seam, so that a part which cannot break the running game is separable from
// one that replaces it.
//
// These records are variable length -- a snapshot carries as many systems as the galaxy has -- so
// unlike the two above they are not fixed-extent spans with a compile-time size. Every list is a
// 16-bit count followed by that many elements, and every read is bounds-checked: a record arriving
// over a socket is attacker-shaped input, and the deserializer's contract is that a malformed or
// truncated buffer is `false` rather than a read past the end.

/// How much a seat knows about one system (ADR-017).
///
/// The topology is public, so a system is always on the map at its coordinates with its lanes and
/// their tick costs. What is graded is its CONTENTS.
enum class Visibility : std::uint8_t
{
  /// Never observed by this seat. Its shape is on the map; its owner, yield and garrison are not.
  Unknown,
  /// Observed before, not now. The contents are as of `observedTick`, and the client draws them as
  /// stale -- the tick stamp is what makes "this is what I saw, not what is there" sayable.
  Remembered,
  /// A fleet of this seat is at it or within `scoutingRevealLanes` of it. Contents are current.
  Observed
};

/// One system as one seat sees it.
struct SystemView
{
  std::uint16_t systemId;
  std::int32_t xUnits;
  std::int32_t yUnits;
  std::uint8_t kind;
  Visibility visibility;
  /// The holder, or 0xFF for unowned or unknown.
  std::uint8_t owner;
  std::uint8_t reserved;
  /// Zero under Unknown. Never a guess: a seat is shown what it saw or nothing.
  std::int32_t yieldPerTick;
  std::int32_t garrisonStrength;
  /// The tick the contents were observed. Zero under Unknown.
  std::uint64_t observedTick;
};

/// A lane. Public in full, because distance is authored and the one-pager makes lanes public.
struct LaneView
{
  std::uint16_t laneId;
  std::uint16_t endA;
  std::uint16_t endB;
  std::int32_t costTicks;
};

/// A fleet in transit. Public to everyone with its tick-ETA: the one-pager makes a departed fleet
/// visible in transit, which is what makes commitment blind at the moment of choice and public
/// afterwards.
struct TransitView
{
  std::uint16_t fleetId;
  std::uint16_t laneId;
  std::uint16_t towardSystemId;
  std::uint8_t owner;
  std::uint8_t reserved;
  std::int32_t strength;
  std::uint64_t departedTick;
  std::uint64_t arrivesTick;
};

/// What everyone knows about a seat: the public score the one-pager insists on, and the capital
/// guard countdown it makes an explicit visible rule.
struct SeatView
{
  std::uint8_t seatId;
  std::uint8_t reserved0;
  std::uint16_t capitalSystemId;
  std::int32_t score;
  std::uint64_t capitalGuardEndsTick;
};

/// Everything one seat may see after a tick (ADR-005). Complete, never a delta: a seat that has
/// been away for a week needs no history to be right.
struct VisibleSnapshot
{
  std::uint64_t tick;
  std::uint64_t endTick;
  /// Visible from tick one, which is why it is in every snapshot rather than announced.
  std::uint64_t sealedOpensTick;
  std::uint8_t seat;
  std::vector<SystemView> systems;
  std::vector<LaneView> lanes;
  std::vector<TransitView> transits;
  std::vector<SeatView> seats;
};

/// What a frame carries. A wire value: numbered explicitly, never renumbered, and appended to.
enum class MessageType : std::uint16_t
{
  None = 0,
  VisibleSnapshot = 1
};

/// Bumped when a record's layout changes. A frame whose version this build does not know is
/// refused with a message rather than deserialized into nonsense.
inline constexpr std::uint16_t PROTOCOL_VERSION = 1;

/// 4 bytes of payload length, 2 of type, 2 of version.
inline constexpr std::size_t FRAME_HEADER_BYTES = 8;

/// A bound on one frame, so a malformed length cannot ask for an allocation. Estimated, not
/// measured: a twelve-seat snapshot is on the order of tens of kilobytes (ADR-005's open question),
/// and this is two orders above that.
inline constexpr std::uint32_t MAX_FRAME_PAYLOAD_BYTES = 4u * 1024u * 1024u;

struct Frame
{
  MessageType type;
  std::uint16_t version;
  std::vector<std::byte> payload;
};

void EncodeFrame(const Frame& _frame, std::vector<std::byte>& _outBytes);

/// False when the buffer is short, the length disagrees with it, the payload is over the cap, or
/// the version is not this build's.
[[nodiscard]] bool DecodeFrame(std::span<const std::byte> _bytes, Frame& _outFrame);

void Serialize(const VisibleSnapshot& _snapshot, std::vector<std::byte>& _outBytes);

/// False on a truncated or malformed buffer. `_outSnapshot` is then unspecified and must not be
/// read -- there is no partial success.
[[nodiscard]] bool DeserializeVisibleSnapshot(std::span<const std::byte> _bytes, VisibleSnapshot& _outSnapshot);

} // namespace Neuron
