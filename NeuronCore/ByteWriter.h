#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

/// Appends primitives to a byte buffer, little-endian, with no framing of its own.
///
/// This is the layer R2 said to wait for. Nothing needed it until an `OrderSet` had to survive a
/// round trip (4X-01 step 3); a byte writer built at step 1, when the only caller would have been
/// its own test, would have been a format frozen before anything used it.
///
/// **Little-endian by explicit shifting, not by memcpy.** The tree is x64 only, so a `memcpy` of
/// the object representation would work today and would be a trap: the day this feeds a socket
/// (4X-02) or a file, the format has to be a decision rather than whatever the compiler laid out.
/// Shifting says what the bytes are, and says it identically on any machine that ever compiles it.
///
/// There is no type tagging and no length prefix on the whole. A reader knows the shape because it
/// is the same code that wrote it, one version at a time. Versioning belongs to the protocol that
/// carries this, and that protocol is 4X-02's to design.
class ByteWriter
{
public:
  void WriteU8(std::uint8_t _value)
  {
    m_bytes.push_back(_value);
  }

  void WriteU16(std::uint16_t _value)
  {
    WriteU8(static_cast<std::uint8_t>(_value & 0xFFU));
    WriteU8(static_cast<std::uint8_t>((_value >> 8) & 0xFFU));
  }

  void WriteU32(std::uint32_t _value)
  {
    for (std::int32_t byte = 0; byte < 4; ++byte)
    {
      WriteU8(static_cast<std::uint8_t>((_value >> (byte * 8)) & 0xFFU));
    }
  }

  void WriteU64(std::uint64_t _value)
  {
    for (std::int32_t byte = 0; byte < 8; ++byte)
    {
      WriteU8(static_cast<std::uint8_t>((_value >> (byte * 8)) & 0xFFULL));
    }
  }

  /// Signed values go over as their two's-complement bit pattern, converted explicitly rather than
  /// by a cast that is only well defined one way round. `Id`'s NONE is -1, and it has to come back
  /// as -1.
  void WriteI32(std::int32_t _value)
  {
    WriteU32(static_cast<std::uint32_t>(_value));
  }

  void WriteBool(bool _value)
  {
    WriteU8(_value ? 1U : 0U);
  }

  /// A 16-bit length and then the bytes. Names in this game are system names and player names;
  /// 65535 is far past anything the font can draw, and a 32-bit length on every string would cost
  /// two bytes each to allow for a string nothing produces.
  void WriteString(std::string_view _value)
  {
    const auto length = static_cast<std::uint16_t>(_value.size() > 0xFFFFU ? 0xFFFFU : _value.size());
    WriteU16(length);
    for (std::uint16_t index = 0; index < length; ++index)
    {
      WriteU8(static_cast<std::uint8_t>(_value[index]));
    }
  }

  [[nodiscard]] const std::vector<std::uint8_t>& Bytes() const noexcept
  {
    return m_bytes;
  }

  [[nodiscard]] std::size_t Size() const noexcept
  {
    return m_bytes.size();
  }

  void Clear() noexcept
  {
    m_bytes.clear();
  }

private:
  std::vector<std::uint8_t> m_bytes;
};

} // namespace Neuron
