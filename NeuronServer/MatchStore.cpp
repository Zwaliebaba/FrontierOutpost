// MatchStore.cpp -- a match on disk, as its configuration and its turns.
//
// Everything here treats its input as hostile, and that is not paranoia about attackers so much as
// realism about crashes: this file is rewritten four times a day for three weeks, and the one thing
// certain to happen to it is that a process dies partway through writing it.

#include "pch.h"
#include "MatchStore.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <cstdio>

namespace Neuron
{

const char* MatchStore::Describe(Problem _problem) noexcept
{
  switch (_problem)
  {
  case Problem::None:
    return "read";
  case Problem::NotFound:
    return "no match store there";
  case Problem::NotAStore:
    return "not a match store, or written by an incompatible build";
  case Problem::Truncated:
    return "the match store is cut short";
  default:
    return "unknown";
  }
}

std::vector<std::uint8_t> MatchStore::Encode(const Contents& _contents)
{
  ByteWriter writer;
  writer.WriteU32(MAGIC);
  writer.WriteU32(VERSION);
  writer.WriteU64(_contents.hash);

  writer.WriteU32(static_cast<std::uint32_t>(_contents.configuration.size()));
  for (const std::uint8_t byte : _contents.configuration)
  {
    writer.WriteU8(byte);
  }

  writer.WriteU32(static_cast<std::uint32_t>(_contents.ticks.size()));
  for (const std::vector<PlayerTurn>& tick : _contents.ticks)
  {
    writer.WriteU32(static_cast<std::uint32_t>(tick.size()));
    for (const PlayerTurn& turn : tick)
    {
      writer.WriteBool(turn.present);
      writer.WriteU32(static_cast<std::uint32_t>(turn.orders.size()));
      for (const std::uint8_t byte : turn.orders)
      {
        writer.WriteU8(byte);
      }
    }
  }

  return writer.Bytes();
}

MatchStore::Problem MatchStore::Decode(std::span<const std::uint8_t> _bytes, Contents& _outContents)
{
  _outContents = Contents{};

  ByteReader reader{_bytes};
  const std::uint32_t magic = reader.ReadU32();
  const std::uint32_t version = reader.ReadU32();

  // Checked before anything else is read, so a file that is not a store is refused rather than
  // interpreted -- the difference between "that is not a match store" and a garbage match.
  if (reader.Failed() || magic != MAGIC || version != VERSION)
  {
    return Problem::NotAStore;
  }

  _outContents.hash = reader.ReadU64();

  const std::uint32_t configurationSize = reader.ReadU32();
  if (reader.Failed() || configurationSize > MAXIMUM_BLOB_BYTES || configurationSize > reader.Remaining())
  {
    return Problem::Truncated;
  }
  _outContents.configuration.reserve(configurationSize);
  for (std::uint32_t index = 0; index < configurationSize; ++index)
  {
    _outContents.configuration.push_back(reader.ReadU8());
  }

  const std::uint32_t tickCount = reader.ReadU32();
  if (reader.Failed() || tickCount > MAXIMUM_TICKS)
  {
    return Problem::Truncated;
  }

  _outContents.ticks.reserve(tickCount);
  for (std::uint32_t tick = 0; tick < tickCount; ++tick)
  {
    const std::uint32_t playerCount = reader.ReadU32();
    if (reader.Failed() || playerCount > MAXIMUM_PLAYERS)
    {
      return Problem::Truncated;
    }

    std::vector<PlayerTurn> turns;
    turns.reserve(playerCount);
    for (std::uint32_t player = 0; player < playerCount; ++player)
    {
      PlayerTurn turn;
      turn.present = reader.ReadBool();

      const std::uint32_t orderBytes = reader.ReadU32();
      if (reader.Failed() || orderBytes > MAXIMUM_BLOB_BYTES || orderBytes > reader.Remaining())
      {
        return Problem::Truncated;
      }

      turn.orders.reserve(orderBytes);
      for (std::uint32_t index = 0; index < orderBytes; ++index)
      {
        turn.orders.push_back(reader.ReadU8());
      }
      turns.push_back(std::move(turn));
    }
    _outContents.ticks.push_back(std::move(turns));
  }

  if (reader.Failed())
  {
    return Problem::Truncated;
  }

  // Bytes left over mean the file is not the shape this reader expects, which is the same kind of
  // wrong as it being short.
  return reader.AtEnd() ? Problem::None : Problem::NotAStore;
}

bool MatchStore::Save(const std::string& _path, const Contents& _contents)
{
  const std::vector<std::uint8_t> bytes = Encode(_contents);

  // Paths are UTF-8 in this tree and the CRT's narrow file functions take the system code page, so
  // the conversion happens here, once, at the one place a path meets the filesystem.
  const std::wstring path = Utf8ToWide(_path);
  const std::wstring temporary = path + L".writing";

  {
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, temporary.c_str(), L"wb") != 0 || file == nullptr)
    {
      return false;
    }

    const std::size_t written = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), file);
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;

    if (written != bytes.size() || !flushed || !closed)
    {
      (void)_wremove(temporary.c_str());
      return false;
    }
  }

  // Rename over the target. A process that dies before this line leaves the previous store whole;
  // one that dies after it leaves the new one whole. There is no instant at which the file on disk
  // is half of either.
  (void)_wremove(path.c_str());
  if (_wrename(temporary.c_str(), path.c_str()) != 0)
  {
    (void)_wremove(temporary.c_str());
    return false;
  }
  return true;
}

MatchStore::Problem MatchStore::Load(const std::string& _path, Contents& _outContents)
{
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, Utf8ToWide(_path).c_str(), L"rb") != 0 || file == nullptr)
  {
    return Problem::NotFound;
  }

  std::vector<std::uint8_t> bytes;
  std::uint8_t buffer[4096];
  while (const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file))
  {
    bytes.insert(bytes.end(), buffer, buffer + read);
  }
  (void)std::fclose(file);

  return Decode(bytes, _outContents);
}

} // namespace Neuron
