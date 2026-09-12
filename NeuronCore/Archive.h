#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Neuron
{

/// A reader or a writer, behind one interface, so a record is described once.
///
/// **Three wire formats in this tree were hand-mirrored** — a `Write` and a `Read` per record, the
/// fields listed twice, in the same order, by hand. The rules format was the first to stop
/// (`MATCH_RULES_FIELDS`, one list driving both directions and the field count), and this is the
/// same idea without the macro: a record says what it is made of once, and which direction it is
/// going is the archive's business.
///
/// **What that buys is not brevity.** Two lists that must agree are two lists that eventually do
/// not, and the failure is silent: a field added to the writer and forgotten in the reader decodes
/// the rest of the record shifted by four bytes, which is a valid-looking record of nonsense. That
/// is not hypothetical here — the rules format had a count guard that compared a constant with
/// itself, and the constant was 34 for 38 fields written.
///
/// **It is also the one place an enumerator is range-checked.** Every decoder in this tree has to
/// refuse a byte that names no enumerator, and before this each did it in its own way or not at
/// all.
///
/// It is deliberately not a serialization framework: no versioning, no schema, no reflection. It is
/// eight primitives and a direction.
class Archive
{
public:
  explicit Archive(ByteWriter& _writer) noexcept
    : m_writer(&_writer)
  {
  }
  explicit Archive(ByteReader& _reader) noexcept
    : m_reader(&_reader)
  {
  }

  [[nodiscard]] bool Writing() const noexcept
  {
    return m_writer != nullptr;
  }

  /// Whether anything has gone wrong: a read past the end, a count past its bound, or a byte that
  /// names no enumerator. Always false while writing.
  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed || (m_reader != nullptr && m_reader->Failed());
  }

  void U8(std::uint8_t& _value)
  {
    if (m_writer != nullptr)
    {
      m_writer->WriteU8(_value);
    }
    else
    {
      _value = m_reader->ReadU8();
    }
  }

  void U32(std::uint32_t& _value)
  {
    if (m_writer != nullptr)
    {
      m_writer->WriteU32(_value);
    }
    else
    {
      _value = m_reader->ReadU32();
    }
  }

  void U64(std::uint64_t& _value)
  {
    if (m_writer != nullptr)
    {
      m_writer->WriteU64(_value);
    }
    else
    {
      _value = m_reader->ReadU64();
    }
  }

  void I32(std::int32_t& _value)
  {
    if (m_writer != nullptr)
    {
      m_writer->WriteI32(_value);
    }
    else
    {
      _value = m_reader->ReadI32();
    }
  }

  void Boolean(bool& _value)
  {
    if (m_writer != nullptr)
    {
      m_writer->WriteBool(_value);
    }
    else
    {
      _value = m_reader->ReadBool();
    }
  }

  void Text(std::string& _value)
  {
    if (m_writer != nullptr)
    {
      m_writer->WriteString(_value);
    }
    else
    {
      _value = m_reader->ReadString();
    }
  }

  /// An `Id<Tag>`, as its underlying index, invalid included.
  ///
  /// `Id::NONE` is -1 and survives the trip as an unset id rather than as zero — which would be
  /// system zero, the sealed region, and a plausible-looking lie.
  template <typename Identifier> void Identity(Identifier& _id)
  {
    std::int32_t index = _id.Index();
    I32(index);
    if (!Writing())
    {
      _id = Identifier{index};
    }
  }

  /// An enumerator, refused on the way in if it names nothing.
  ///
  /// `_last` is the highest enumerator the type has, which is the idiom `ByteReader::ReadEnum`
  /// already uses and which this delegates to rather than repeating: a byte past it is a byte from
  /// something that is not this program, and the reader fails rather than carrying a value the
  /// switch below it has no case for.
  template <typename Enumeration> void Enumerator(Enumeration& _value, Enumeration _last)
  {
    if (m_writer != nullptr)
    {
      m_writer->WriteU8(static_cast<std::uint8_t>(_value));
      return;
    }
    _value = m_reader->ReadEnum(_last);
  }

  /// A count, bounded on the way in.
  ///
  /// **The bound is the whole reason this exists.** A truncated or hostile record can declare four
  /// billion entries, and a `reserve` on that is an allocation failure at best. Returns the count
  /// to loop over, which is zero when the archive has already failed.
  [[nodiscard]] std::uint32_t Count(std::size_t _size, std::uint32_t _maximum)
  {
    auto count = static_cast<std::uint32_t>(_size);
    U32(count);
    if (Writing())
    {
      return count;
    }
    if (count > _maximum)
    {
      m_failed = true;
      return 0;
    }
    return Failed() ? 0 : count;
  }

private:
  ByteWriter* m_writer = nullptr;
  ByteReader* m_reader = nullptr;
  bool m_failed = false;
};

} // namespace Neuron
