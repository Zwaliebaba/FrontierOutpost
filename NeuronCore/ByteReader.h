#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace Neuron
{

/// Reads back what `ByteWriter` wrote.
///
/// **A read past the end is not undefined and not an exception: it sets `Failed` and returns
/// zero.** The bytes this will one day be handed come off a socket (4X-02), which means they can
/// be truncated, malformed or hostile, and a reader whose error path is a crash is a denial of
/// service with extra steps. Checking `Failed()` once after decoding a whole record is both
/// cheaper and harder to get wrong than checking a return value on every field -- and a record
/// decoded from a short buffer is uniformly zeros rather than partly garbage.
///
/// Zero is a deliberate choice of wrong answer. Every id in this game is `Id::NONE` at -1, so a
/// truncated record decodes to ids that are *valid-looking but zero* rather than to nonsense --
/// which is why `Failed()` is the check that matters and no caller should trust the values without
/// it.
class ByteReader
{
public:
  explicit ByteReader(std::span<const std::uint8_t> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  [[nodiscard]] std::uint8_t ReadU8() noexcept
  {
    if (m_at >= m_bytes.size())
    {
      m_failed = true;
      return 0;
    }
    return m_bytes[m_at++];
  }

  [[nodiscard]] std::uint16_t ReadU16() noexcept
  {
    const std::uint16_t low = ReadU8();
    const auto high = static_cast<std::uint16_t>(ReadU8() << 8);
    return static_cast<std::uint16_t>(low | high);
  }

  [[nodiscard]] std::uint32_t ReadU32() noexcept
  {
    std::uint32_t value = 0;
    for (std::int32_t byte = 0; byte < 4; ++byte)
    {
      value |= static_cast<std::uint32_t>(ReadU8()) << (byte * 8);
    }
    return value;
  }

  [[nodiscard]] std::uint64_t ReadU64() noexcept
  {
    std::uint64_t value = 0;
    for (std::int32_t byte = 0; byte < 8; ++byte)
    {
      value |= static_cast<std::uint64_t>(ReadU8()) << (byte * 8);
    }
    return value;
  }

  [[nodiscard]] std::int32_t ReadI32() noexcept
  {
    return static_cast<std::int32_t>(ReadU32());
  }

  [[nodiscard]] bool ReadBool() noexcept
  {
    return ReadU8() != 0;
  }

  /// Refuses a length the buffer cannot hold rather than reading that many bytes one at a time and
  /// failing 65535 times. A declared length longer than what is left is the signature of a
  /// truncated or a lying record, and either way there is nothing to return.
  [[nodiscard]] std::string ReadString()
  {
    const std::uint16_t length = ReadU16();
    if (m_failed || (m_bytes.size() - m_at) < length)
    {
      m_failed = true;
      return {};
    }

    std::string value;
    value.reserve(length);
    for (std::uint16_t index = 0; index < length; ++index)
    {
      value.push_back(static_cast<char>(ReadU8()));
    }
    return value;
  }

  /// True once any read has run past the end. Never clears itself: one bad field poisons the
  /// record, which is the point.
  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed;
  }

  /// True when the buffer was consumed exactly. A record that decoded without failing but left
  /// bytes over is a record whose shape does not match the reader's -- which a test should catch
  /// and a socket should reject.
  [[nodiscard]] bool AtEnd() const noexcept
  {
    return !m_failed && m_at == m_bytes.size();
  }

  [[nodiscard]] std::size_t Remaining() const noexcept
  {
    return m_bytes.size() - m_at;
  }

private:
  std::span<const std::uint8_t> m_bytes;
  std::size_t m_at = 0;
  bool m_failed = false;
};

} // namespace Neuron
