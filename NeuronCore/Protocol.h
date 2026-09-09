#pragma once

#include "Trigonometry.h"

#include <cstddef>
#include <cstdint>
#include <span>

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

} // namespace Neuron
