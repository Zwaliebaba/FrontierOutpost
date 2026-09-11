// FrameStream.cpp -- whole messages out of a byte stream.

#include "pch.h"
#include "FrameStream.h"

#include "ByteReader.h"
#include "ByteWriter.h"

namespace Neuron
{

namespace
{
constexpr std::size_t HEADER_BYTES = 4;
}

std::vector<std::uint8_t> FrameStream::Frame(std::span<const std::uint8_t> _payload)
{
  ByteWriter writer;
  writer.WriteU32(static_cast<std::uint32_t>(_payload.size()));

  std::vector<std::uint8_t> framed = writer.Bytes();
  framed.insert(framed.end(), _payload.begin(), _payload.end());
  return framed;
}

bool FrameStream::Feed(std::span<const std::uint8_t> _bytes)
{
  if (m_failed)
  {
    return false;
  }

  m_buffer.insert(m_buffer.end(), _bytes.begin(), _bytes.end());

  // Checked on arrival rather than when the frame completes, so a peer announcing a gigabyte is
  // refused now instead of after it has been allowed to buffer one.
  if (m_buffer.size() >= HEADER_BYTES)
  {
    ByteReader reader{m_buffer};
    const std::uint32_t declared = reader.ReadU32();
    if (declared > MAXIMUM_FRAME_BYTES)
    {
      m_failed = true;
      return false;
    }
  }

  return true;
}

bool FrameStream::Take(std::vector<std::uint8_t>& _outPayload)
{
  _outPayload.clear();
  if (m_failed || m_buffer.size() < HEADER_BYTES)
  {
    return false;
  }

  ByteReader reader{m_buffer};
  const std::uint32_t declared = reader.ReadU32();
  if (declared > MAXIMUM_FRAME_BYTES)
  {
    m_failed = true;
    return false;
  }

  if (m_buffer.size() < HEADER_BYTES + declared)
  {
    return false;
  }

  _outPayload.assign(m_buffer.begin() + HEADER_BYTES, m_buffer.begin() + HEADER_BYTES + declared);

  // Erasing from the front of a vector is O(n), and n here is at most one message plus whatever
  // arrived with it. At four messages a day the alternative -- a ring buffer, or an offset that
  // needs compacting -- is more code than the copy is worth.
  m_buffer.erase(m_buffer.begin(), m_buffer.begin() + HEADER_BYTES + declared);
  return true;
}

void FrameStream::Reset() noexcept
{
  m_buffer.clear();
  m_failed = false;
}

} // namespace Neuron
