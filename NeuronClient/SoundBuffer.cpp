// NeuronClient/SoundBuffer.cpp
#include "pch.h"

#include "SoundBuffer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

namespace Neuron
{

namespace
{

constexpr std::uint16_t FORMAT_PCM = 0x0001;
constexpr std::uint16_t FORMAT_ADPCM = 0x0002;
constexpr std::uint16_t FORMAT_IEEE_FLOAT = 0x0003;
constexpr std::uint16_t FORMAT_EXTENSIBLE = 0xFFFE;

// WAVEFORMATEX, which is what XAudio2 reads the format as: a 16-byte PCM chunk and a cbSize of 0.
constexpr std::size_t WAVEFORMATEX_BYTES = 18;
constexpr std::size_t FMT_MIN_BYTES = 16;
constexpr std::size_t EXTENSIBLE_BYTES = 40;
constexpr std::size_t ADPCM_BYTES = 50;

constexpr std::uint32_t MAX_CHANNELS = 64;    // XAUDIO2_MAX_AUDIO_CHANNELS
constexpr std::uint32_t MIN_RATE_HZ = 1000;   // XAUDIO2_MIN_SAMPLE_RATE
constexpr std::uint32_t MAX_RATE_HZ = 200000; // XAUDIO2_MAX_SAMPLE_RATE
constexpr std::uint16_t ADPCM_COEFFICIENT_COUNT = 7;
constexpr std::array<std::int16_t, 14> ADPCM_COEFFICIENTS = {256, 0, 512, -256, 0, 0, 192, 64, 240, 0, 460, -208, 392, -232};
// The last 12 bytes of KSDATAFORMAT_SUBTYPE_PCM and _IEEE_FLOAT; the first 4 are the format tag.
constexpr std::array<std::uint8_t, 12> SUBFORMAT_TAIL = {0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

std::uint16_t Read16(std::span<const std::byte> _bytes, std::size_t _at)
{
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(_bytes[_at]) | (std::to_integer<unsigned>(_bytes[_at + 1]) << 8));
}

std::uint32_t Read32(std::span<const std::byte> _bytes, std::size_t _at)
{
  return static_cast<std::uint32_t>(Read16(_bytes, _at)) | (static_cast<std::uint32_t>(Read16(_bytes, _at + 2)) << 16);
}

void Write16(std::span<std::byte> _bytes, std::size_t _at, std::uint32_t _value)
{
  _bytes[_at] = static_cast<std::byte>(_value & 0xFF);
  _bytes[_at + 1] = static_cast<std::byte>((_value >> 8) & 0xFF);
}

void Write32(std::span<std::byte> _bytes, std::size_t _at, std::uint32_t _value)
{
  Write16(_bytes, _at, _value & 0xFFFF);
  Write16(_bytes, _at + 2, _value >> 16);
}

bool Is(std::span<const std::byte> _bytes, std::size_t _at, const char* _fourCharacters)
{
  return std::memcmp(_bytes.data() + _at, _fourCharacters, 4) == 0;
}

const char* RefusedName(std::uint16_t _tag)
{
  switch (_tag)
  {
  case 0x0006:
    return " (A-law)";
  case 0x0007:
    return " (mu-law)";
  case 0x0011:
    return " (IMA ADPCM)";
  case 0x0031:
    return " (GSM 6.10)";
  case 0x0050:
    return " (MPEG)";
  case 0x0055:
    return " (MPEG layer 3)";
  case 0x0161:
  case 0x0162:
    return " (xWMA)";
  default:
    return "";
  }
}

/// Why XAudio2 cannot play this format, or an empty string. The same rules, in the same order, as
/// Build/CheckSounds.py's Problem().
std::string Problem(std::span<const std::byte> _fmt, std::size_t _dataBytes)
{
  if (_fmt.size() < FMT_MIN_BYTES)
  {
    return std::format("a fmt chunk of {} bytes, where the engine needs 16", _fmt.size());
  }
  std::uint16_t tag = Read16(_fmt, 0);
  const std::uint16_t channels = Read16(_fmt, 2);
  const std::uint32_t rateHz = Read32(_fmt, 4);
  const std::uint16_t alignBytes = Read16(_fmt, 12);
  const std::uint16_t bits = Read16(_fmt, 14);
  if (alignBytes == 0 || rateHz == 0 || channels == 0)
  {
    return "a block size, sample rate or channel count of zero";
  }
  if (_dataBytes < alignBytes)
  {
    return "no whole block of samples";
  }
  if (channels > MAX_CHANNELS || rateHz < MIN_RATE_HZ || rateHz > MAX_RATE_HZ)
  {
    return std::format("{} channels at {} Hz, outside XAudio2's limits", channels, rateHz);
  }
  if (tag == FORMAT_EXTENSIBLE)
  {
    if (_fmt.size() < EXTENSIBLE_BYTES)
    {
      return "WAVE_FORMAT_EXTENSIBLE in a fmt chunk under 40 bytes";
    }
    const std::uint16_t validBits = Read16(_fmt, 18);
    const std::uint32_t subformat = Read32(_fmt, 24);
    if (std::memcmp(_fmt.data() + 28, SUBFORMAT_TAIL.data(), SUBFORMAT_TAIL.size()) != 0 ||
        (subformat != FORMAT_PCM && subformat != FORMAT_IEEE_FLOAT))
    {
      return "WAVE_FORMAT_EXTENSIBLE with a subformat other than PCM or IEEE float";
    }
    if (validBits > bits)
    {
      return std::format("{} valid bits in a {}-bit sample", validBits, bits);
    }
    tag = static_cast<std::uint16_t>(subformat);
  }
  if (tag == FORMAT_PCM)
  {
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
    {
      return std::format("{}-bit PCM", bits);
    }
    return alignBytes == channels * bits / 8 ? std::string()
                                             : std::format("a block of {} bytes for {} x {}-bit PCM", alignBytes, channels, bits);
  }
  if (tag == FORMAT_IEEE_FLOAT)
  {
    if (bits != 32)
    {
      return std::format("{}-bit IEEE float", bits);
    }
    return alignBytes == channels * 4 ? std::string() : std::format("a block of {} bytes for {} x 32-bit float", alignBytes, channels);
  }
  if (tag == FORMAT_ADPCM)
  {
    if ((channels != 1 && channels != 2) || bits != 4 || _fmt.size() < ADPCM_BYTES)
    {
      return "Microsoft ADPCM that is not 4-bit mono or stereo with its coefficient table";
    }
    const std::uint16_t samplesPerBlock = Read16(_fmt, 18);
    bool standard = Read16(_fmt, 20) == ADPCM_COEFFICIENT_COUNT;
    for (std::size_t i = 0; standard && i < ADPCM_COEFFICIENTS.size(); ++i)
    {
      standard = static_cast<std::int16_t>(Read16(_fmt, 22 + (2 * i))) == ADPCM_COEFFICIENTS[i];
    }
    if (!standard)
    {
      return "Microsoft ADPCM with non-standard coefficients";
    }
    // Signed and wide, as the checker computes it: a block too short for its header gives a
    // negative count.
    const std::int64_t wideChannels = channels;
    const std::int64_t expected = ((alignBytes - (7 * wideChannels)) * 8 / (4 * wideChannels)) + 2;
    if (samplesPerBlock != expected || (channels == 1 && samplesPerBlock % 2 != 0))
    {
      return std::format("Microsoft ADPCM with {} samples in a {}-byte block", samplesPerBlock, alignBytes);
    }
    return {};
  }
  return std::format("format 0x{:04X}{}, which XAudio2 does not play", tag, RefusedName(tag));
}

} // namespace

bool SoundBuffer::FromWav(std::span<const std::byte> _fileBytes, SoundBuffer& _outBuffer, std::string& _error)
{
  if (_fileBytes.size() < 12 || !Is(_fileBytes, 0, "RIFF") || !Is(_fileBytes, 8, "WAVE"))
  {
    _error = "not a RIFF WAVE file";
    return false;
  }
  std::span<const std::byte> fmt;
  std::span<const std::byte> data;
  bool hasFmt = false;
  bool hasData = false;
  for (std::size_t at = 12; at + 8 <= _fileBytes.size();)
  {
    const std::uint32_t chunkBytes = Read32(_fileBytes, at + 4);
    const std::size_t length = std::min<std::size_t>(chunkBytes, _fileBytes.size() - (at + 8));
    if (Is(_fileBytes, at, "fmt "))
    {
      fmt = _fileBytes.subspan(at + 8, length);
      hasFmt = true;
    }
    else if (Is(_fileBytes, at, "data"))
    {
      data = _fileBytes.subspan(at + 8, length);
      hasData = true;
    }
    at += 8 + static_cast<std::size_t>(chunkBytes) + (chunkBytes & 1);
  }
  if (!hasFmt || !hasData)
  {
    _error = hasFmt ? "no data chunk" : "no fmt chunk";
    return false;
  }
  _error = Problem(fmt, data.size());
  if (!_error.empty())
  {
    return false;
  }

  SoundBuffer buffer;
  buffer.m_format.assign(std::max(fmt.size(), WAVEFORMATEX_BYTES), std::byte{0});
  std::ranges::copy(fmt, buffer.m_format.begin());
  buffer.m_channels = Read16(fmt, 2);
  buffer.m_sampleRateHz = Read32(fmt, 4);
  const std::uint16_t alignBytes = Read16(fmt, 12);
  if (Read16(fmt, 0) == FORMAT_ADPCM)
  {
    buffer.m_framesPerBlock = std::max<std::uint32_t>(Read16(fmt, 18), 1);
  }
  // XAudio2 takes whole blocks only.
  const std::size_t blocks = data.size() / alignBytes;
  buffer.m_samples.assign(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(blocks * alignBytes));
  buffer.m_frames = static_cast<std::uint32_t>(blocks * buffer.m_framesPerBlock);
  _outBuffer = std::move(buffer);
  return true;
}

SoundBuffer SoundBuffer::FromSamples(std::span<const float> _samples, std::uint32_t _sampleRateHz)
{
  SoundBuffer buffer;
  buffer.m_format.assign(WAVEFORMATEX_BYTES, std::byte{0});
  const std::span<std::byte> format = buffer.m_format;
  Write16(format, 0, FORMAT_IEEE_FLOAT);
  Write16(format, 2, 1);
  Write32(format, 4, _sampleRateHz);
  Write32(format, 8, _sampleRateHz * static_cast<std::uint32_t>(sizeof(float)));
  Write16(format, 12, sizeof(float));
  Write16(format, 14, 32);
  const std::span<const std::byte> samples = std::as_bytes(_samples);
  buffer.m_samples.assign(samples.begin(), samples.end());
  buffer.m_channels = 1;
  buffer.m_sampleRateHz = _sampleRateHz;
  buffer.m_frames = static_cast<std::uint32_t>(_samples.size());
  return buffer;
}

float SoundBuffer::DurationMs() const noexcept
{
  return m_sampleRateHz == 0 ? 0.0f : 1000.0f * static_cast<float>(m_frames) / static_cast<float>(m_sampleRateHz);
}

} // namespace Neuron
