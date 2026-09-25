// NeuronClient/SoundBuffer.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

/// A sound's format and samples, as XAudio2 plays them (Design/ADR/ADR-003): PCM at 8, 16, 24 or
/// 32 bits, 32-bit IEEE float, either of those through WAVE_FORMAT_EXTENSIBLE, or Microsoft ADPCM
/// with its standard coefficients, in 1 to 64 channels at 1,000 to 200,000 Hz.
class SoundBuffer
{
public:
  /// Reads a RIFF WAVE file's bytes. On failure returns false and says why in _error, in the words
  /// Build/CheckSounds.py uses for the same file.
  [[nodiscard]] static bool FromWav(std::span<const std::byte> _fileBytes, SoundBuffer& _outBuffer, std::string& _error);

  /// Mono 32-bit float samples at _sampleRateHz.
  [[nodiscard]] static SoundBuffer FromSamples(std::span<const float> _samples, std::uint32_t _sampleRateHz);

  [[nodiscard]] std::uint32_t Channels() const noexcept
  {
    return m_channels;
  }

  [[nodiscard]] std::uint32_t SampleRateHz() const noexcept
  {
    return m_sampleRateHz;
  }

  /// Frames hold one sample for each channel.
  [[nodiscard]] std::uint32_t Frames() const noexcept
  {
    return m_frames;
  }

  /// ADPCM decodes a block at a time, so a voice starts only on a multiple of this.
  [[nodiscard]] std::uint32_t FramesPerBlock() const noexcept
  {
    return m_framesPerBlock;
  }

  [[nodiscard]] float DurationMs() const noexcept;

  /// The format, laid out as a WAVEFORMATEX and what follows it, and the samples: whole blocks.
  [[nodiscard]] std::span<const std::byte> FormatBytes() const noexcept
  {
    return m_format;
  }

  [[nodiscard]] std::span<const std::byte> SampleBytes() const noexcept
  {
    return m_samples;
  }

private:
  std::vector<std::byte> m_format;
  std::vector<std::byte> m_samples;
  std::uint32_t m_channels = 0;
  std::uint32_t m_sampleRateHz = 0;
  std::uint32_t m_frames = 0;
  std::uint32_t m_framesPerBlock = 1;
};

} // namespace Neuron
