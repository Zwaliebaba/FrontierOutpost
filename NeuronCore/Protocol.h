#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

/// What a message is. The first byte of every frame's payload.
///
/// Six kinds, and the list is short on purpose: this game sends four messages a day per player, and
/// a protocol with room to grow is a protocol somebody grows.
enum class MessageKind : std::uint8_t
{
  /// Not a message. What an empty payload, or one whose first byte names nothing, decodes to.
  ///
  /// A named enumerator rather than a cast of zero: `KindOf` has to answer something for a payload
  /// with no bytes in it, and a value every switch has to handle deserves a name rather than a
  /// number every reader has to decode.
  ///
  /// It is never sent. A frame carrying it would be a frame whose first byte is zero, which is what
  /// this value exists to reject.
  None = 0,
  /// Client to server, first thing: a token. Nothing else is accepted before it.
  Hello = 1,
  /// Server to client: you are this player, and here is where the match is.
  Welcome = 2,
  /// Server to client: no. Carries a reason, because "it didn't work" is not a bug report.
  Refused = 3,
  /// Client to server: my order set for the next lock, replacing whatever I sent before.
  Orders = 4,
  /// Server to client: a snapshot and a digest, after every lock.
  State = 5,
  /// Client to server: still here. Presence, and the thing that keeps a NAT mapping alive.
  Ping = 6
};

/// Why a `Hello` was refused.
enum class RefusalReason : std::uint8_t
{
  None = 0,
  /// The token is not on this match's list.
  UnknownToken = 1,
  /// It is, and somebody is already connected on it.
  AlreadyConnected = 2,
  /// The match is over.
  MatchFinished = 3,
  /// The message did not decode, or arrived out of order.
  Malformed = 4
};

[[nodiscard]] const char* Describe(RefusalReason _reason) noexcept;

/// The messages, as encoders and decoders.
///
/// **Every decoder returns false rather than throwing, and checks `AtEnd`.** These bytes come off a
/// socket: they can be truncated, padded, or written by something that is not this program. A
/// decoder that trusted a length would be the whole attack surface.
namespace Protocol
{

[[nodiscard]] std::vector<std::uint8_t> EncodeHello(const std::string& _token);
[[nodiscard]] bool DecodeHello(std::span<const std::uint8_t> _payload, std::string& _outToken);

[[nodiscard]] std::vector<std::uint8_t> EncodeWelcome(std::int32_t _player, std::uint32_t _tick, std::int64_t _secondsToLock);
[[nodiscard]] bool DecodeWelcome(std::span<const std::uint8_t> _payload, std::int32_t& _outPlayer, std::uint32_t& _outTick,
                                 std::int64_t& _outSecondsToLock);

[[nodiscard]] std::vector<std::uint8_t> EncodeRefused(RefusalReason _reason);
[[nodiscard]] bool DecodeRefused(std::span<const std::uint8_t> _payload, RefusalReason& _outReason);

[[nodiscard]] std::vector<std::uint8_t> EncodeOrders(std::span<const std::uint8_t> _orderSet);
[[nodiscard]] bool DecodeOrders(std::span<const std::uint8_t> _payload, std::vector<std::uint8_t>& _outOrderSet);

/// One resolved tick's digest, and which tick it is.
///
/// **A `State` carries a LIST of these** (ADR-044). It used to carry one, which was right while a
/// client was assumed to be watching: the digest was always the tick that had just resolved and the
/// tick number was the message's own. A client coming back after a night is owed the ones it
/// missed, and a digest with no tick on it cannot be filed against what the player last read.
struct TickDigest
{
  std::uint32_t tick = 0;
  std::vector<std::uint8_t> bytes;
};

/// The digests a `State` may carry. Bounded on the wire because it is bounded at the server
/// (`Session::DIGEST_HISTORY`), and a decoder that trusted a count from a peer would be a decoder
/// that allocates whatever it is told to.
inline constexpr std::uint32_t MAXIMUM_DIGESTS = 32;

[[nodiscard]] std::vector<std::uint8_t> EncodeState(std::uint32_t _tick, std::int64_t _secondsToLock,
                                                    std::span<const std::uint8_t> _snapshot, std::span<const TickDigest> _digests);
[[nodiscard]] bool DecodeState(std::span<const std::uint8_t> _payload, std::uint32_t& _outTick, std::int64_t& _outSecondsToLock,
                               std::vector<std::uint8_t>& _outSnapshot, std::vector<TickDigest>& _outDigests);

[[nodiscard]] std::vector<std::uint8_t> EncodePing();

/// The kind of a payload, or zero if it does not have one. Cheap, and the first thing a reader
/// does.
[[nodiscard]] MessageKind KindOf(std::span<const std::uint8_t> _payload) noexcept;

/// The most bytes a blob inside a message may claim. A snapshot is a few kilobytes; anything near
/// this is a peer that has stopped making sense.
inline constexpr std::uint32_t MAXIMUM_BLOB_BYTES = 500'000;

/// The longest a token may be. Long enough to be unguessable, short enough that a refusal costs
/// nothing to send.
inline constexpr std::size_t MAXIMUM_TOKEN_LENGTH = 64;

} // namespace Protocol

} // namespace Neuron
