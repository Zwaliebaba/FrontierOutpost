// NeuronClient/AudioDevice.h
#pragma once

#include "Float3.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Neuron
{

class AudioDevice;
class SoundBuffer;

/// Where the listener faces and how fast it moves. The listener stands at the origin, so an
/// emitter's position is relative to it.
struct Listener
{
  Float3 front{0.0f, 0.0f, 1.0f};
  Float3 top{0.0f, 1.0f, 0.0f};
  Float3 velocity; // units per second, as the device's speed of sound is
};

/// Where a voice plays from, relative to the listener, and how it fades with distance.
struct Emitter
{
  Float3 position;
  Float3 velocity; // units per second
  /// Up to this distance the voice plays at full volume. Beyond it the volume falls as
  /// minDistance / distance: X3DAudio's default curve, which is FMOD's inverse rolloff.
  float minDistance = 1.0f;
};

/// FMOD Ex's pan law, as the output matrix XAudio2 takes, element [output * _inChannels + input],
/// with outputs 0 and 1 the front left and right. A mono input is panned at constant power, so
/// that centered it plays at 71% on both sides; a stereo input has one side faded towards the
/// other. With one output, every input is mixed into it equally. _pan runs from -1, left, to 1,
/// right. Returns false, and leaves _matrix alone, for more than two inputs, no output, or a
/// matrix smaller than _inChannels * _outChannels.
[[nodiscard]] bool PanMatrix(std::uint32_t _inChannels, std::uint32_t _outChannels, float _pan, std::span<float> _matrix) noexcept;

/// One sound, playing or ready to: an XAudio2 source voice with its buffer queued. A voice the
/// device could not make is empty; an empty voice is finished and ignores every call. A voice
/// holds its SoundBuffer until it is destroyed. When the device goes first, the voice is empty
/// from then on.
class Voice
{
public:
  Voice() noexcept;
  ~Voice();
  Voice(Voice&& _other) noexcept;
  Voice& operator=(Voice&& _other) noexcept;
  Voice(const Voice&) = delete;
  Voice& operator=(const Voice&) = delete;

  /// True unless the voice is empty.
  explicit operator bool() const noexcept;

  /// True when the voice is empty, or has played all it had queued. A looped voice never finishes.
  [[nodiscard]] bool IsFinished() const noexcept;

  void Start();
  void Stop();
  void SetVolume(float _volume);

  /// Sets the output matrix by PanMatrix. A voice that Spatialize places is not panned as well.
  void SetPan(float _pan);

  /// Playback speed against the sound's own sample rate. The Doppler shift multiplies it, and the
  /// product is held between XAudio2's minimum and the device's maxFrequencyRatio.
  void SetFrequencyRatio(float _ratio);

  /// Plays from _frame, rounded down to a whole block, or from the start if that is past the end.
  /// In call order, the voice stops, drops what it had queued, queues the sound from there, and
  /// starts again if it had been started.
  void Seek(std::uint32_t _frame);

  /// Direction, distance and Doppler shift, by X3DAudio. A voice of more than 8 channels is left
  /// as it is.
  void Spatialize(const Emitter& _emitter);

private:
  friend class AudioDevice;
  struct State;

  explicit Voice(std::unique_ptr<State> _state) noexcept;
  [[nodiscard]] State* Playable() const noexcept;

  std::unique_ptr<State> m_state;
};

/// Plays sounds on the default audio device with XAudio2, and places them with X3DAudio
/// (Design/ADR/ADR-003). A machine without an audio device is not a fault: the device is silent,
/// and every voice it makes is empty. It is not thread-safe: one thread makes it, uses it and its
/// voices, and destroys it.
class AudioDevice
{
public:
  struct Desc
  {
    float speedOfSoundUnitsPerSecond; // for the Doppler shift
    std::uint32_t maxVoices;          // voices at once; CreateVoice makes no more
    float maxFrequencyRatio;          // pitch and Doppler shift together, as Voice takes them
    /// A call to XAudio2 failed. The device and the voice concerned carry on as if it had not been
    /// made; a device that failed while it was being made is silent.
    std::function<void(const std::string&)> onFailure;
    /// No audio device, or no free voice.
    std::function<void(const std::string&)> onWarning;
  };

  explicit AudioDevice(Desc _desc);
  ~AudioDevice();
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  [[nodiscard]] bool IsSilent() const noexcept;
  [[nodiscard]] std::uint32_t OutputChannels() const noexcept;
  [[nodiscard]] std::uint32_t VoicesInUse() const noexcept;

  /// X3DAudio wants an orthonormal listener: front is normalized, and top made perpendicular to it
  /// and normalized. A listener whose front or top is zero, or whose top is parallel to its
  /// front, is ignored.
  void SetListener(const Listener& _listener) noexcept;

  /// A voice for _buffer, stopped, with the sound queued once, or over and over if _looped.
  /// Without an audio device, a free voice or any samples, the voice is empty. _name is for the
  /// messages that name the sound.
  [[nodiscard]] Voice CreateVoice(std::shared_ptr<const SoundBuffer> _buffer, bool _looped, std::string_view _name);

private:
  friend class Voice;
  struct Native;

  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
