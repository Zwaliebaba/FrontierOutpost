#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

/// Turns a byte stream into whole messages, and back.
///
/// TCP is a stream and not a sequence of messages: a send of 300 bytes can arrive as 40 and 260, or
/// as the tail of one message and the head of the next. **Everything that reads a socket needs
/// this, and it is the part people reimplement wrong**, so it is one class with no socket in it at
/// all -- feed it whatever arrived and take out whatever is complete.
///
/// The frame is a 32-bit little-endian length followed by that many bytes. Nothing else: no kind,
/// no version, no checksum. The kind is the first byte of the payload (`Protocol.h`), TCP already
/// checksums, and a version belongs to the payload rather than to the envelope.
///
/// **Every length off the wire is checked before it is believed.** A frame claiming four billion
/// bytes is a hostile or broken peer, and the answer is to fail the connection rather than to
/// reserve.
class FrameStream
{
public:
  /// The largest frame this will accept. A snapshot of a twelve-player galaxy is a few kilobytes;
  /// a megabyte is far past anything legitimate and far below anything that hurts to refuse.
  static constexpr std::uint32_t MAXIMUM_FRAME_BYTES = 1'000'000;

  /// Wraps a payload in its length. Free function in spirit -- it holds nothing.
  [[nodiscard]] static std::vector<std::uint8_t> Frame(std::span<const std::uint8_t> _payload);

  /// Adds whatever arrived. Returns false if the peer has sent something impossible, after which
  /// this stream is poisoned and the connection should be dropped.
  [[nodiscard]] bool Feed(std::span<const std::uint8_t> _bytes);

  /// Takes the next complete message, or false if there is not one yet.
  [[nodiscard]] bool Take(std::vector<std::uint8_t>& _outPayload);

  /// True once the peer has said something impossible. Never clears: one bad length means nothing
  /// after it can be trusted, because the stream is no longer aligned to anything.
  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed;
  }

  /// Bytes held but not yet a whole message. A connection that sits on a huge partial frame
  /// forever is a slow-loris, and the server can see it here.
  [[nodiscard]] std::size_t Pending() const noexcept
  {
    return m_buffer.size();
  }

  void Reset() noexcept;

private:
  std::vector<std::uint8_t> m_buffer;
  bool m_failed = false;
};

} // namespace Neuron
