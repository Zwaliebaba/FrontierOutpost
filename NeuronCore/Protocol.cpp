// Protocol.cpp -- the wire records, field by field, little-endian.

#include "pch.h"
#include "Protocol.h"

namespace Neuron
{

namespace
{

/// Writes an integer little-endian, whatever the machine's byte order is, and advances the
/// cursor. Shifting rather than memcpy'ing is the point: a shift means the same thing on every
/// machine, and this format has to survive the day one of them is big-endian.
template <typename T> void Write(std::span<std::byte>& _cursor, T _value) noexcept
{
  using Unsigned = std::make_unsigned_t<T>;
  auto bits = static_cast<Unsigned>(_value);

  for (std::size_t byte = 0; byte < sizeof(T); ++byte)
  {
    _cursor[byte] = static_cast<std::byte>(bits & 0xFFU);
    bits = static_cast<Unsigned>(bits >> 8);
  }

  _cursor = _cursor.subspan(sizeof(T));
}

template <typename T> [[nodiscard]] T Read(std::span<const std::byte>& _cursor) noexcept
{
  using Unsigned = std::make_unsigned_t<T>;
  Unsigned bits = 0;

  for (std::size_t byte = 0; byte < sizeof(T); ++byte)
  {
    bits = static_cast<Unsigned>(bits | (static_cast<Unsigned>(_cursor[byte]) << (8 * byte)));
  }

  _cursor = _cursor.subspan(sizeof(T));
  return static_cast<T>(bits);
}

} // namespace

// The sizes are the format. A field added without widening these is a build error here rather
// than a record that deserializes into garbage somewhere downstream.
//
// They are also what makes the four functions below safe without a runtime check: the spans have
// a fixed extent, Write and Read advance by exactly sizeof(T), and these assertions say the two
// add up. A DEBUG_ASSERT that the cursor ended empty would be asserting something the compiler
// has already proved -- and would make a noexcept function able to throw, since Fatal does.
static_assert(MOVE_TO_ORDER_BYTES == sizeof(MoveToOrder::targetXMillimetres) + sizeof(MoveToOrder::targetZMillimetres));
static_assert(SHIP_STATE_BYTES == sizeof(ShipState::tick) + sizeof(ShipState::positionXMillimetres) +
                                    sizeof(ShipState::positionZMillimetres) + sizeof(ShipState::speedMillimetresPerTick) +
                                    sizeof(ShipState::headingTurns16) + sizeof(ShipState::reserved));

void Serialize(const MoveToOrder& _order, std::span<std::byte, MOVE_TO_ORDER_BYTES> _bytes) noexcept
{
  std::span<std::byte> cursor = _bytes;
  Write(cursor, _order.targetXMillimetres);
  Write(cursor, _order.targetZMillimetres);
}

MoveToOrder DeserializeMoveToOrder(std::span<const std::byte, MOVE_TO_ORDER_BYTES> _bytes) noexcept
{
  std::span<const std::byte> cursor = _bytes;
  MoveToOrder order = {};
  order.targetXMillimetres = Read<std::int64_t>(cursor);
  order.targetZMillimetres = Read<std::int64_t>(cursor);
  return order;
}

void Serialize(const ShipState& _state, std::span<std::byte, SHIP_STATE_BYTES> _bytes) noexcept
{
  std::span<std::byte> cursor = _bytes;
  Write(cursor, _state.tick);
  Write(cursor, _state.positionXMillimetres);
  Write(cursor, _state.positionZMillimetres);
  Write(cursor, _state.speedMillimetresPerTick);
  Write(cursor, _state.headingTurns16);
  Write(cursor, _state.reserved);
}

ShipState DeserializeShipState(std::span<const std::byte, SHIP_STATE_BYTES> _bytes) noexcept
{
  std::span<const std::byte> cursor = _bytes;
  ShipState state = {};
  state.tick = Read<std::uint64_t>(cursor);
  state.positionXMillimetres = Read<std::int64_t>(cursor);
  state.positionZMillimetres = Read<std::int64_t>(cursor);
  state.speedMillimetresPerTick = Read<std::int32_t>(cursor);
  state.headingTurns16 = Read<Turns16>(cursor);
  state.reserved = Read<std::uint16_t>(cursor);
  return state;
}

} // namespace Neuron
