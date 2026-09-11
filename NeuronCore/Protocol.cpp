// Protocol.cpp -- six messages, and a decoder for each that assumes the sender is hostile.

#include "pch.h"
#include "Protocol.h"

namespace Neuron
{

const char* Describe(RefusalReason _reason) noexcept
{
  switch (_reason)
  {
  case RefusalReason::None:
    return "accepted";
  case RefusalReason::UnknownToken:
    return "that token is not on this match";
  case RefusalReason::AlreadyConnected:
    return "somebody is already connected on that token";
  case RefusalReason::MatchFinished:
    return "the match is over";
  case RefusalReason::Malformed:
    return "the message did not make sense";
  default:
    return "unknown";
  }
}

namespace Protocol
{

namespace
{

void WriteBlob(ByteWriter& _writer, std::span<const std::uint8_t> _blob)
{
  _writer.WriteU32(static_cast<std::uint32_t>(_blob.size()));
  for (const std::uint8_t byte : _blob)
  {
    _writer.WriteU8(byte);
  }
}

/// Reads a length-prefixed blob, refusing a length the buffer cannot possibly hold.
///
/// The second check is the one that matters. A declared size under the cap but larger than what is
/// left is a truncated or lying message, and reading it a byte at a time would work -- filling the
/// tail with zeros, which is a message that decodes into something plausible and wrong.
[[nodiscard]] bool ReadBlob(ByteReader& _reader, std::vector<std::uint8_t>& _out)
{
  const std::uint32_t declared = _reader.ReadU32();
  if (_reader.Failed() || declared > MAXIMUM_BLOB_BYTES || declared > _reader.Remaining())
  {
    return false;
  }

  _out.clear();
  _out.reserve(declared);
  for (std::uint32_t index = 0; index < declared; ++index)
  {
    _out.push_back(_reader.ReadU8());
  }
  return !_reader.Failed();
}

[[nodiscard]] ByteReader Opened(std::span<const std::uint8_t> _payload, MessageKind _expected, bool& _outOk)
{
  ByteReader reader{_payload};
  const auto kind = static_cast<MessageKind>(reader.ReadU8());
  _outOk = !reader.Failed() && kind == _expected;
  return reader;
}

} // namespace

MessageKind KindOf(std::span<const std::uint8_t> _payload) noexcept
{
  return _payload.empty() ? static_cast<MessageKind>(0) : static_cast<MessageKind>(_payload[0]);
}

std::vector<std::uint8_t> EncodeHello(const std::string& _token)
{
  ByteWriter writer;
  writer.WriteU8(static_cast<std::uint8_t>(MessageKind::Hello));
  writer.WriteString(_token);
  return writer.Bytes();
}

bool DecodeHello(std::span<const std::uint8_t> _payload, std::string& _outToken)
{
  bool ok = false;
  ByteReader reader = Opened(_payload, MessageKind::Hello, ok);
  if (!ok)
  {
    return false;
  }

  _outToken = reader.ReadString();
  return !reader.Failed() && reader.AtEnd() && _outToken.size() <= MAXIMUM_TOKEN_LENGTH;
}

std::vector<std::uint8_t> EncodeWelcome(std::int32_t _player, std::uint32_t _tick, std::int64_t _secondsToLock)
{
  ByteWriter writer;
  writer.WriteU8(static_cast<std::uint8_t>(MessageKind::Welcome));
  writer.WriteI32(_player);
  writer.WriteU32(_tick);
  writer.WriteU64(static_cast<std::uint64_t>(_secondsToLock));
  return writer.Bytes();
}

bool DecodeWelcome(std::span<const std::uint8_t> _payload, std::int32_t& _outPlayer, std::uint32_t& _outTick,
                   std::int64_t& _outSecondsToLock)
{
  bool ok = false;
  ByteReader reader = Opened(_payload, MessageKind::Welcome, ok);
  if (!ok)
  {
    return false;
  }

  _outPlayer = reader.ReadI32();
  _outTick = reader.ReadU32();
  _outSecondsToLock = static_cast<std::int64_t>(reader.ReadU64());
  return !reader.Failed() && reader.AtEnd();
}

std::vector<std::uint8_t> EncodeRefused(RefusalReason _reason)
{
  ByteWriter writer;
  writer.WriteU8(static_cast<std::uint8_t>(MessageKind::Refused));
  writer.WriteU8(static_cast<std::uint8_t>(_reason));
  return writer.Bytes();
}

bool DecodeRefused(std::span<const std::uint8_t> _payload, RefusalReason& _outReason)
{
  bool ok = false;
  ByteReader reader = Opened(_payload, MessageKind::Refused, ok);
  if (!ok)
  {
    return false;
  }

  _outReason = static_cast<RefusalReason>(reader.ReadU8());
  return !reader.Failed() && reader.AtEnd();
}

std::vector<std::uint8_t> EncodeOrders(std::span<const std::uint8_t> _orderSet)
{
  ByteWriter writer;
  writer.WriteU8(static_cast<std::uint8_t>(MessageKind::Orders));
  WriteBlob(writer, _orderSet);
  return writer.Bytes();
}

bool DecodeOrders(std::span<const std::uint8_t> _payload, std::vector<std::uint8_t>& _outOrderSet)
{
  bool ok = false;
  ByteReader reader = Opened(_payload, MessageKind::Orders, ok);
  if (!ok)
  {
    return false;
  }

  return ReadBlob(reader, _outOrderSet) && reader.AtEnd();
}

std::vector<std::uint8_t> EncodeState(std::uint32_t _tick, std::int64_t _secondsToLock, std::span<const std::uint8_t> _snapshot,
                                      std::span<const std::uint8_t> _digest)
{
  ByteWriter writer;
  writer.WriteU8(static_cast<std::uint8_t>(MessageKind::State));
  writer.WriteU32(_tick);
  writer.WriteU64(static_cast<std::uint64_t>(_secondsToLock));
  WriteBlob(writer, _snapshot);
  WriteBlob(writer, _digest);
  return writer.Bytes();
}

bool DecodeState(std::span<const std::uint8_t> _payload, std::uint32_t& _outTick, std::int64_t& _outSecondsToLock,
                 std::vector<std::uint8_t>& _outSnapshot, std::vector<std::uint8_t>& _outDigest)
{
  bool ok = false;
  ByteReader reader = Opened(_payload, MessageKind::State, ok);
  if (!ok)
  {
    return false;
  }

  _outTick = reader.ReadU32();
  _outSecondsToLock = static_cast<std::int64_t>(reader.ReadU64());
  return ReadBlob(reader, _outSnapshot) && ReadBlob(reader, _outDigest) && reader.AtEnd();
}

std::vector<std::uint8_t> EncodePing()
{
  ByteWriter writer;
  writer.WriteU8(static_cast<std::uint8_t>(MessageKind::Ping));
  return writer.Bytes();
}

} // namespace Protocol

} // namespace Neuron
