// Tests/NeuronClientTests/SoundParsing.cpp
//
// Neuron::SoundBuffer reads the WAV files XAudio2 plays, and refuses the others in the words
// Build/CheckSounds.py uses for them (Design/ADR/ADR-003, N9). The files are written here byte by
// byte, as Build/TestCheckers.py writes the checker's.
#include "pch.h"

#include "Check.h"
#include "Repository.h"
#include "SoundBuffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

// Microsoft ADPCM's seven standard coefficient pairs.
constexpr std::array<std::int16_t, 14> ADPCM_COEFFICIENTS = {256, 0, 512, -256, 0, 0, 192, 64, 240, 0, 460, -208, 392, -232};

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT.
constexpr std::array<std::uint8_t, 16> FLOAT_SUBFORMAT = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                                          0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

void Put16(std::vector<std::byte>& _out, std::uint32_t _value)
{
  _out.push_back(static_cast<std::byte>(_value & 0xFF));
  _out.push_back(static_cast<std::byte>((_value >> 8) & 0xFF));
}

void Put32(std::vector<std::byte>& _out, std::uint32_t _value)
{
  Put16(_out, _value & 0xFFFF);
  Put16(_out, _value >> 16);
}

void PutText(std::vector<std::byte>& _out, std::string_view _text)
{
  for (const char character : _text)
  {
    _out.push_back(static_cast<std::byte>(character));
  }
}

void PutChunk(std::vector<std::byte>& _out, std::string_view _id, std::span<const std::byte> _body)
{
  PutText(_out, _id);
  Put32(_out, static_cast<std::uint32_t>(_body.size()));
  _out.insert(_out.end(), _body.begin(), _body.end());
  if (_body.size() % 2 != 0)
  {
    _out.push_back(std::byte{0});
  }
}

/// A WAV file's parts. The defaults make 16-bit stereo PCM at 44,100 Hz.
struct WavSpec
{
  std::uint16_t tag = 1;
  std::uint16_t channels = 2;
  std::uint32_t rateHz = 44100;
  std::uint16_t bits = 16;
  std::uint16_t alignBytes = 0;      // 0: channels * bits / 8
  std::vector<std::byte> extra{};    // after the 16-byte format
  int dataBytes = 16;                // no data chunk when negative
  std::vector<std::byte> listBody{}; // a LIST chunk before the format, when there is one
};

/// The sample bytes Wav writes: 0, 1, 2 and so on.
std::vector<std::byte> Samples(std::size_t _bytes)
{
  std::vector<std::byte> samples(_bytes);
  for (std::size_t i = 0; i < _bytes; ++i)
  {
    samples[i] = static_cast<std::byte>(i & 0xFF);
  }
  return samples;
}

std::vector<std::byte> Wav(const WavSpec& _spec)
{
  const std::uint16_t alignBytes = _spec.alignBytes != 0 ? _spec.alignBytes : static_cast<std::uint16_t>(_spec.channels * _spec.bits / 8);
  std::vector<std::byte> chunks;
  if (!_spec.listBody.empty())
  {
    PutChunk(chunks, "LIST", _spec.listBody);
  }
  std::vector<std::byte> format;
  Put16(format, _spec.tag);
  Put16(format, _spec.channels);
  Put32(format, _spec.rateHz);
  Put32(format, _spec.rateHz * alignBytes);
  Put16(format, alignBytes);
  Put16(format, _spec.bits);
  format.insert(format.end(), _spec.extra.begin(), _spec.extra.end());
  PutChunk(chunks, "fmt ", format);
  if (_spec.dataBytes >= 0)
  {
    PutChunk(chunks, "data", Samples(static_cast<std::size_t>(_spec.dataBytes)));
  }
  std::vector<std::byte> file;
  PutText(file, "RIFF");
  Put32(file, static_cast<std::uint32_t>(4 + chunks.size()));
  PutText(file, "WAVE");
  file.insert(file.end(), chunks.begin(), chunks.end());
  return file;
}

/// ADPCM's format past the first 16 bytes: cbSize, the samples in a block, and the coefficients.
std::vector<std::byte> AdpcmExtra(std::uint16_t _samplesPerBlock, std::span<const std::int16_t> _coefficients)
{
  std::vector<std::byte> extra;
  Put16(extra, 32);
  Put16(extra, _samplesPerBlock);
  Put16(extra, static_cast<std::uint32_t>(_coefficients.size() / 2));
  for (const std::int16_t coefficient : _coefficients)
  {
    Put16(extra, static_cast<std::uint16_t>(coefficient));
  }
  return extra;
}

Neuron::SoundBuffer Read(const std::vector<std::byte>& _file)
{
  Neuron::SoundBuffer buffer;
  std::string error;
  Assert::IsTrue(Neuron::SoundBuffer::FromWav(_file, buffer, error), Widen(error).c_str());
  return buffer;
}

/// Why FromWav refuses the file, which it must.
std::string Refusal(const std::vector<std::byte>& _file)
{
  Neuron::SoundBuffer buffer;
  std::string error;
  Assert::IsFalse(Neuron::SoundBuffer::FromWav(_file, buffer, error), L"a file XAudio2 cannot play was read");
  return error;
}

void AssertRefused(const std::vector<std::byte>& _file, std::string_view _expected)
{
  const std::string error = Refusal(_file);
  Assert::IsTrue(error == _expected, Widen(error).c_str());
}

std::uint32_t FormatField16(const Neuron::SoundBuffer& _buffer, std::size_t _at)
{
  const std::span<const std::byte> format = _buffer.FormatBytes();
  return std::to_integer<std::uint32_t>(format[_at]) | (std::to_integer<std::uint32_t>(format[_at + 1]) << 8);
}

} // namespace

TEST_CLASS(SoundParsing)
{
public:
  TEST_METHOD(ReadsPcm)
  {
    const Neuron::SoundBuffer buffer = Read(Wav({.dataBytes = 40}));
    Assert::AreEqual(2u, buffer.Channels());
    Assert::AreEqual(44100u, buffer.SampleRateHz());
    Assert::AreEqual(10u, buffer.Frames());
    Assert::AreEqual(1u, buffer.FramesPerBlock());
    // A 16-byte PCM format reads as a WAVEFORMATEX whose cbSize is 0.
    Assert::AreEqual(std::size_t{18}, buffer.FormatBytes().size());
    Assert::AreEqual(1u, FormatField16(buffer, 0));
    Assert::AreEqual(0u, FormatField16(buffer, 16));
    Assert::IsTrue(std::ranges::equal(buffer.SampleBytes(), Samples(40)), L"the samples differ");
  }

  TEST_METHOD(KeepsWholeBlocksOnly)
  {
    const Neuron::SoundBuffer buffer = Read(Wav({.dataBytes = 42}));
    Assert::AreEqual(10u, buffer.Frames());
    Assert::AreEqual(std::size_t{40}, buffer.SampleBytes().size());
  }

  TEST_METHOD(ReadsFloatAndItsDuration)
  {
    const Neuron::SoundBuffer buffer = Read(Wav({.tag = 3, .channels = 1, .rateHz = 1000, .bits = 32, .dataBytes = 400}));
    Assert::AreEqual(1u, buffer.Channels());
    Assert::AreEqual(100u, buffer.Frames());
    Assert::AreEqual(100.0f, buffer.DurationMs());
  }

  TEST_METHOD(ReadsFloatThroughExtensible)
  {
    std::vector<std::byte> extra;
    Put16(extra, 22); // cbSize
    Put16(extra, 32); // valid bits
    Put32(extra, 3);  // front left and right
    for (const std::uint8_t byte : FLOAT_SUBFORMAT)
    {
      extra.push_back(static_cast<std::byte>(byte));
    }
    const Neuron::SoundBuffer buffer = Read(Wav({.tag = 0xFFFE, .bits = 32, .extra = extra, .dataBytes = 64}));
    Assert::AreEqual(8u, buffer.Frames());
    Assert::AreEqual(std::size_t{40}, buffer.FormatBytes().size());
    Assert::AreEqual(0xFFFEu, FormatField16(buffer, 0));
  }

  TEST_METHOD(ReadsAdpcmInWholeBlocks)
  {
    // 256-byte mono blocks hold 7 bytes of header and 498 samples of 4 bits, plus the two samples
    // in the header.
    const Neuron::SoundBuffer buffer =
      Read(Wav({.tag = 2, .channels = 1, .bits = 4, .alignBytes = 256, .extra = AdpcmExtra(500, ADPCM_COEFFICIENTS), .dataBytes = 600}));
    Assert::AreEqual(500u, buffer.FramesPerBlock());
    Assert::AreEqual(1000u, buffer.Frames());
    Assert::AreEqual(std::size_t{512}, buffer.SampleBytes().size());
    Assert::AreEqual(std::size_t{50}, buffer.FormatBytes().size());
  }

  TEST_METHOD(SkipsOtherChunksAndTheirPadding)
  {
    const std::vector<std::byte> list = {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    const Neuron::SoundBuffer buffer = Read(Wav({.dataBytes = 8, .listBody = list}));
    Assert::AreEqual(2u, buffer.Frames());
  }

  TEST_METHOD(ReadsTheGamesSounds)
  {
    const std::filesystem::path sounds = Repository() / L"GameData" / L"sound";
    int files = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(sounds))
    {
      if (!entry.is_regular_file() || entry.path().extension() != L".wav")
      {
        continue;
      }
      std::ifstream stream(entry.path(), std::ios::binary);
      const std::vector<char> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
      Neuron::SoundBuffer buffer;
      std::string error;
      Assert::IsTrue(Neuron::SoundBuffer::FromWav(std::as_bytes(std::span(bytes)), buffer, error),
                     (entry.path().wstring() + L": " + Widen(error)).c_str());
      Assert::IsTrue(buffer.Frames() > 0, entry.path().c_str());
      ++files;
    }
    Assert::IsTrue(files > 0, L"GameData/sound holds no WAV file");
  }

  TEST_METHOD(RefusesWhatXAudio2DoesNotPlay)
  {
    AssertRefused(Wav({.tag = 0x11, .bits = 4, .alignBytes = 512, .dataBytes = 512}),
                  "format 0x0011 (IMA ADPCM), which XAudio2 does not play");
    AssertRefused(Wav({.tag = 0x161, .dataBytes = 16}), "format 0x0161 (xWMA), which XAudio2 does not play");
    AssertRefused(Wav({.bits = 12, .alignBytes = 4}), "12-bit PCM");
    AssertRefused(Wav({.tag = 2,
                       .channels = 1,
                       .bits = 4,
                       .alignBytes = 256,
                       .extra = AdpcmExtra(500, std::array<std::int16_t, 14>{}),
                       .dataBytes = 256}),
                  "Microsoft ADPCM with non-standard coefficients");
    AssertRefused(
      Wav({.tag = 2, .channels = 1, .bits = 4, .alignBytes = 256, .extra = AdpcmExtra(498, ADPCM_COEFFICIENTS), .dataBytes = 256}),
      "Microsoft ADPCM with 498 samples in a 256-byte block");
    AssertRefused(Wav({.channels = 70, .dataBytes = 280}), "70 channels at 44100 Hz, outside XAudio2's limits");
  }

  TEST_METHOD(RefusesWhatIsNoSound)
  {
    AssertRefused(std::vector<std::byte>(64), "not a RIFF WAVE file");
    AssertRefused(Wav({.dataBytes = -1}), "no data chunk");
    std::vector<std::byte> noFormat = Wav({});
    noFormat[12] = std::byte{'j'}; // "fmt " becomes "jmt "
    AssertRefused(noFormat, "no fmt chunk");
    AssertRefused(Wav({.dataBytes = 2}), "no whole block of samples");
  }

  TEST_METHOD(LeavesTheBufferAloneWhenItRefuses)
  {
    Neuron::SoundBuffer buffer = Read(Wav({.dataBytes = 40}));
    std::string error;
    Assert::IsFalse(Neuron::SoundBuffer::FromWav(Wav({.bits = 12, .alignBytes = 4}), buffer, error));
    Assert::AreEqual(10u, buffer.Frames());
  }

  TEST_METHOD(HoldsSamplesItIsGiven)
  {
    std::vector<float> samples(441);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
      samples[i] = std::sin(static_cast<float>(i) * 0.1f);
    }
    const Neuron::SoundBuffer buffer = Neuron::SoundBuffer::FromSamples(samples, 44100);
    Assert::AreEqual(1u, buffer.Channels());
    Assert::AreEqual(44100u, buffer.SampleRateHz());
    Assert::AreEqual(441u, buffer.Frames());
    Assert::AreEqual(10.0f, buffer.DurationMs());
    Assert::AreEqual(3u, FormatField16(buffer, 0));  // IEEE float
    Assert::AreEqual(4u, FormatField16(buffer, 12)); // bytes in a frame
    Assert::AreEqual(32u, FormatField16(buffer, 14));
    Assert::IsTrue(std::ranges::equal(buffer.SampleBytes(), std::as_bytes(std::span(samples))), L"the samples differ");
  }
};

} // namespace NeuronClientTests
