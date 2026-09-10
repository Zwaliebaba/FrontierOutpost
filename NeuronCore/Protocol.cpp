// Protocol.cpp -- the wire records, field by field, little-endian.

#include "pch.h"
#include "Protocol.h"

#include <utility>

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

// ---------------------------------------------------------------------------------------------
// The 4X protocol. Variable length, so the fixed-extent spans above do not apply and every read
// has to be checked.

namespace
{

/// Appends an integer little-endian. Shifts rather than a memcpy, for the same reason as above: a
/// shift means the same thing on every machine and a struct's layout does not.
template <typename T> void Append(std::vector<std::byte>& _bytes, T _value)
{
  using Unsigned = std::make_unsigned_t<T>;
  auto bits = static_cast<Unsigned>(_value);

  for (std::size_t byte = 0; byte < sizeof(T); ++byte)
  {
    _bytes.push_back(static_cast<std::byte>(bits & 0xFFU));
    bits = static_cast<Unsigned>(bits >> 8);
  }
}

/// A cursor that cannot read past its end.
///
/// The failure is sticky: once a read has run off the end, every later read fails too and the
/// caller can check once at the end instead of after every field. That is what keeps the
/// deserializers below readable, and it is the difference between a record that refuses a
/// truncated buffer and one that reads whatever follows it in memory.
class Reader
{
public:
  explicit Reader(std::span<const std::byte> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  template <typename T> [[nodiscard]] T Read() noexcept
  {
    using Unsigned = std::make_unsigned_t<T>;

    if (m_failed || m_offset + sizeof(T) > m_bytes.size())
    {
      m_failed = true;
      return T{};
    }

    Unsigned bits = 0;
    for (std::size_t byte = 0; byte < sizeof(T); ++byte)
    {
      bits = static_cast<Unsigned>(bits | (static_cast<Unsigned>(m_bytes[m_offset + byte]) << (8 * byte)));
    }
    m_offset += sizeof(T);
    return static_cast<T>(bits);
  }

  /// A list length, refused when the buffer cannot possibly hold that many elements of
  /// `_elementBytes`. Checking here rather than at the first element is what stops a malformed
  /// count from reserving a gigabyte before failing.
  [[nodiscard]] std::size_t ReadCount(std::size_t _elementBytes) noexcept
  {
    const auto count = static_cast<std::size_t>(Read<std::uint16_t>());
    if (m_failed || count * _elementBytes > m_bytes.size() - m_offset)
    {
      m_failed = true;
      return 0;
    }
    return count;
  }

  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed;
  }
  [[nodiscard]] bool AtEnd() const noexcept
  {
    return m_offset == m_bytes.size();
  }

private:
  std::span<const std::byte> m_bytes;
  std::size_t m_offset = 0;
  bool m_failed = false;
};

constexpr std::size_t SYSTEM_VIEW_BYTES = 2 + 4 + 4 + 1 + 1 + 1 + 1 + 4 + 4 + 8;
constexpr std::size_t LANE_VIEW_BYTES = 2 + 2 + 2 + 4;
constexpr std::size_t TRANSIT_VIEW_BYTES = 2 + 2 + 2 + 1 + 1 + 4 + 8 + 8;
constexpr std::size_t SEAT_VIEW_BYTES = 1 + 1 + 2 + 4 + 8;

} // namespace

void EncodeFrame(const Frame& _frame, std::vector<std::byte>& _outBytes)
{
  _outBytes.clear();
  _outBytes.reserve(FRAME_HEADER_BYTES + _frame.payload.size());
  Append(_outBytes, static_cast<std::uint32_t>(_frame.payload.size()));
  Append(_outBytes, static_cast<std::uint16_t>(_frame.type));
  Append(_outBytes, _frame.version);
  _outBytes.insert(_outBytes.end(), _frame.payload.begin(), _frame.payload.end());
}

bool DecodeFrame(std::span<const std::byte> _bytes, Frame& _outFrame)
{
  Reader reader{_bytes};
  const auto length = reader.Read<std::uint32_t>();
  const auto type = reader.Read<std::uint16_t>();
  const auto version = reader.Read<std::uint16_t>();

  if (reader.Failed() || length > MAX_FRAME_PAYLOAD_BYTES || _bytes.size() != FRAME_HEADER_BYTES + length)
  {
    return false;
  }
  if (version != PROTOCOL_VERSION)
  {
    return false;
  }

  _outFrame.type = static_cast<MessageType>(type);
  _outFrame.version = version;
  _outFrame.payload.assign(_bytes.begin() + static_cast<std::ptrdiff_t>(FRAME_HEADER_BYTES), _bytes.end());
  return true;
}

void Serialize(const VisibleSnapshot& _snapshot, std::vector<std::byte>& _outBytes)
{
  _outBytes.clear();

  Append(_outBytes, _snapshot.tick);
  Append(_outBytes, _snapshot.endTick);
  Append(_outBytes, _snapshot.sealedOpensTick);
  Append(_outBytes, _snapshot.seat);

  Append(_outBytes, static_cast<std::uint16_t>(_snapshot.systems.size()));
  for (const SystemView& system : _snapshot.systems)
  {
    Append(_outBytes, system.systemId);
    Append(_outBytes, system.xUnits);
    Append(_outBytes, system.yUnits);
    Append(_outBytes, system.kind);
    Append(_outBytes, static_cast<std::uint8_t>(system.visibility));
    Append(_outBytes, system.owner);
    Append(_outBytes, system.reserved);
    Append(_outBytes, system.yieldPerTick);
    Append(_outBytes, system.garrisonStrength);
    Append(_outBytes, system.observedTick);
  }

  Append(_outBytes, static_cast<std::uint16_t>(_snapshot.lanes.size()));
  for (const LaneView& lane : _snapshot.lanes)
  {
    Append(_outBytes, lane.laneId);
    Append(_outBytes, lane.endA);
    Append(_outBytes, lane.endB);
    Append(_outBytes, lane.costTicks);
  }

  Append(_outBytes, static_cast<std::uint16_t>(_snapshot.transits.size()));
  for (const TransitView& transit : _snapshot.transits)
  {
    Append(_outBytes, transit.fleetId);
    Append(_outBytes, transit.laneId);
    Append(_outBytes, transit.towardSystemId);
    Append(_outBytes, transit.owner);
    Append(_outBytes, transit.reserved);
    Append(_outBytes, transit.strength);
    Append(_outBytes, transit.departedTick);
    Append(_outBytes, transit.arrivesTick);
  }

  Append(_outBytes, static_cast<std::uint16_t>(_snapshot.seats.size()));
  for (const SeatView& seat : _snapshot.seats)
  {
    Append(_outBytes, seat.seatId);
    Append(_outBytes, seat.reserved0);
    Append(_outBytes, seat.capitalSystemId);
    Append(_outBytes, seat.score);
    Append(_outBytes, seat.capitalGuardEndsTick);
  }
}

bool DeserializeVisibleSnapshot(std::span<const std::byte> _bytes, VisibleSnapshot& _outSnapshot)
{
  Reader reader{_bytes};
  VisibleSnapshot snapshot;

  snapshot.tick = reader.Read<std::uint64_t>();
  snapshot.endTick = reader.Read<std::uint64_t>();
  snapshot.sealedOpensTick = reader.Read<std::uint64_t>();
  snapshot.seat = reader.Read<std::uint8_t>();

  const std::size_t systemCount = reader.ReadCount(SYSTEM_VIEW_BYTES);
  snapshot.systems.reserve(systemCount);
  for (std::size_t index = 0; index < systemCount; ++index)
  {
    SystemView system = {};
    system.systemId = reader.Read<std::uint16_t>();
    system.xUnits = reader.Read<std::int32_t>();
    system.yUnits = reader.Read<std::int32_t>();
    system.kind = reader.Read<std::uint8_t>();
    system.visibility = static_cast<Visibility>(reader.Read<std::uint8_t>());
    system.owner = reader.Read<std::uint8_t>();
    system.reserved = reader.Read<std::uint8_t>();
    system.yieldPerTick = reader.Read<std::int32_t>();
    system.garrisonStrength = reader.Read<std::int32_t>();
    system.observedTick = reader.Read<std::uint64_t>();
    snapshot.systems.push_back(system);
  }

  const std::size_t laneCount = reader.ReadCount(LANE_VIEW_BYTES);
  snapshot.lanes.reserve(laneCount);
  for (std::size_t index = 0; index < laneCount; ++index)
  {
    LaneView lane = {};
    lane.laneId = reader.Read<std::uint16_t>();
    lane.endA = reader.Read<std::uint16_t>();
    lane.endB = reader.Read<std::uint16_t>();
    lane.costTicks = reader.Read<std::int32_t>();
    snapshot.lanes.push_back(lane);
  }

  const std::size_t transitCount = reader.ReadCount(TRANSIT_VIEW_BYTES);
  snapshot.transits.reserve(transitCount);
  for (std::size_t index = 0; index < transitCount; ++index)
  {
    TransitView transit = {};
    transit.fleetId = reader.Read<std::uint16_t>();
    transit.laneId = reader.Read<std::uint16_t>();
    transit.towardSystemId = reader.Read<std::uint16_t>();
    transit.owner = reader.Read<std::uint8_t>();
    transit.reserved = reader.Read<std::uint8_t>();
    transit.strength = reader.Read<std::int32_t>();
    transit.departedTick = reader.Read<std::uint64_t>();
    transit.arrivesTick = reader.Read<std::uint64_t>();
    snapshot.transits.push_back(transit);
  }

  const std::size_t seatCount = reader.ReadCount(SEAT_VIEW_BYTES);
  snapshot.seats.reserve(seatCount);
  for (std::size_t index = 0; index < seatCount; ++index)
  {
    SeatView seat = {};
    seat.seatId = reader.Read<std::uint8_t>();
    seat.reserved0 = reader.Read<std::uint8_t>();
    seat.capitalSystemId = reader.Read<std::uint16_t>();
    seat.score = reader.Read<std::int32_t>();
    seat.capitalGuardEndsTick = reader.Read<std::uint64_t>();
    snapshot.seats.push_back(seat);
  }

  // Trailing bytes are a mismatch rather than something to ignore: a record that deserializes and
  // leaves a tail is a record whose writer and reader disagree.
  if (reader.Failed() || !reader.AtEnd())
  {
    return false;
  }

  _outSnapshot = std::move(snapshot);
  return true;
}

} // namespace Neuron
