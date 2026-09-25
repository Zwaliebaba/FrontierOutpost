// NeuronClient/AudioDevice.cpp
#include "pch.h"

#include <xaudio2.h>
#include <x3daudio.h>

#include "AudioDevice.h"
#include "ComScope.h"
#include "SoundBuffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "xaudio2.lib")

namespace Neuron
{

namespace
{

/// Channels a spatialized voice may have: X3DAudio places each of them at the emitter.
constexpr std::uint32_t MAX_EMITTER_CHANNELS = 8;

/// X3DAudio takes no curve distance scaler below this.
constexpr float MIN_CURVE_DISTANCE = 1e-3f;

constexpr float MIN_FREQUENCY_RATIO = XAUDIO2_MIN_FREQ_RATIO;

/// A voice is not reference-counted: DestroyVoice destroys it, once.
struct VoiceDeleter
{
  void operator()(IXAudio2Voice* _voice) const noexcept
  {
    _voice->DestroyVoice();
  }
};

template <typename T> using VoicePtr = std::unique_ptr<T, VoiceDeleter>;

void Report(const std::function<void(const std::string&)>& _to, const std::string& _message)
{
  if (_to)
  {
    _to(_message);
  }
}

X3DAUDIO_VECTOR ToVector(const Float3& _vector) noexcept
{
  return {_vector.x, _vector.y, _vector.z};
}

float Dot(const Float3& _a, const Float3& _b) noexcept
{
  return (_a.x * _b.x) + (_a.y * _b.y) + (_a.z * _b.z);
}

float Length(const Float3& _vector) noexcept
{
  return std::sqrt(Dot(_vector, _vector));
}

Float3 Normalize(const Float3& _vector) noexcept
{
  const float length = Length(_vector);
  return length > 0.0f ? Float3{_vector.x / length, _vector.y / length, _vector.z / length} : _vector;
}

} // namespace

struct AudioDevice::Native
{
  Desc desc;
  ComScope com;
  Microsoft::WRL::ComPtr<IXAudio2> xaudio;
  VoicePtr<IXAudio2MasteringVoice> master;
  X3DAUDIO_HANDLE x3d{};
  X3DAUDIO_LISTENER listener{};
  std::uint32_t outputChannels = 0;
  bool silent = false;
  std::vector<Voice::State*> voices; // every voice that has an XAudio2 voice
  std::vector<float> matrix;         // the output matrix, rebuilt for each call

  /// Reports a failed call. Returns whether it succeeded.
  bool Check(HRESULT _result, const char* _call, std::string_view _subject = {}) const
  {
    if (SUCCEEDED(_result))
    {
      return true;
    }
    std::string message = std::format("XAudio2: {} failed with 0x{:x}", _call, static_cast<unsigned long>(_result));
    if (!_subject.empty())
    {
      message += std::format(" for {}", _subject);
    }
    Report(desc.onFailure, message);
    return false;
  }

  void GoSilent(HRESULT _result)
  {
    if (silent)
    {
      return;
    }
    silent = true;
    Report(desc.onWarning, std::format("XAudio2: no audio device (0x{:x}), so there is no sound", static_cast<unsigned long>(_result)));
  }
};

struct Voice::State
{
  AudioDevice::Native* device = nullptr; // null once the device is gone
  std::shared_ptr<const SoundBuffer> buffer;
  VoicePtr<IXAudio2SourceVoice> voice; // after the buffer, so that it is destroyed first
  float frequencyRatio = 1.0f;
  float dopplerFactor = 1.0f;
  bool looped = false;
  bool started = false;

  State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;

  ~State()
  {
    if (device != nullptr)
    {
      std::erase(device->voices, this);
    }
  }

  /// Queues the sound from _frame to its end, then all of it over and over if it loops. A buffer
  /// that starts part-way must say how long it is, and may not loop back to before its start, so a
  /// looped sound started part-way is two buffers: the rest of it once, then all of it in a loop.
  void Submit(std::uint32_t _frame) const
  {
    const std::span<const std::byte> samples = buffer->SampleBytes();
    XAUDIO2_BUFFER all{};
    all.AudioBytes = static_cast<UINT32>(samples.size());
    all.pAudioData = reinterpret_cast<const BYTE*>(samples.data());
    if (_frame > 0)
    {
      XAUDIO2_BUFFER rest = all;
      rest.PlayBegin = _frame;
      rest.PlayLength = buffer->Frames() - _frame;
      if (!looped)
      {
        rest.Flags = XAUDIO2_END_OF_STREAM;
      }
      if (!device->Check(voice->SubmitSourceBuffer(&rest), "IXAudio2SourceVoice::SubmitSourceBuffer") || !looped)
      {
        return;
      }
    }
    all.Flags = XAUDIO2_END_OF_STREAM;
    if (looped)
    {
      all.LoopCount = XAUDIO2_LOOP_INFINITE;
    }
    device->Check(voice->SubmitSourceBuffer(&all), "IXAudio2SourceVoice::SubmitSourceBuffer");
  }

  void ApplyFrequency() const
  {
    const float ratio = std::clamp(frequencyRatio * dopplerFactor, MIN_FREQUENCY_RATIO, device->desc.maxFrequencyRatio);
    device->Check(voice->SetFrequencyRatio(ratio), "IXAudio2SourceVoice::SetFrequencyRatio");
  }

  void SetOutputMatrix() const
  {
    device->Check(voice->SetOutputMatrix(device->master.get(), buffer->Channels(), device->outputChannels, device->matrix.data()),
                  "IXAudio2SourceVoice::SetOutputMatrix");
  }
};

bool PanMatrix(std::uint32_t _inChannels, std::uint32_t _outChannels, float _pan, std::span<float> _matrix) noexcept
{
  const std::size_t elements = static_cast<std::size_t>(_inChannels) * _outChannels;
  if (_inChannels == 0 || _inChannels > 2 || _outChannels == 0 || _matrix.size() < elements)
  {
    return false;
  }
  const float pan = std::clamp(_pan, -1.0f, 1.0f);
  std::fill_n(_matrix.begin(), elements, 0.0f);
  if (_outChannels == 1)
  {
    for (std::uint32_t input = 0; input < _inChannels; ++input)
    {
      _matrix[input] = 1.0f / static_cast<float>(_inChannels);
    }
  }
  else if (_inChannels == 1)
  {
    const float angle = (pan + 1.0f) * 0.25f * std::numbers::pi_v<float>;
    _matrix[0] = std::cos(angle);
    _matrix[1] = std::sin(angle);
  }
  else
  {
    _matrix[0] = pan <= 0.0f ? 1.0f : 1.0f - pan;
    _matrix[3] = pan >= 0.0f ? 1.0f : 1.0f + pan;
  }
  return true;
}

Voice::Voice() noexcept = default;
Voice::~Voice() = default;
Voice::Voice(Voice&&) noexcept = default;
Voice& Voice::operator=(Voice&&) noexcept = default;

Voice::Voice(std::unique_ptr<State> _state) noexcept
  : m_state(std::move(_state))
{
}

Voice::operator bool() const noexcept
{
  return Playable() != nullptr;
}

Voice::State* Voice::Playable() const noexcept
{
  return m_state && m_state->device != nullptr ? m_state.get() : nullptr;
}

bool Voice::IsFinished() const noexcept
{
  const State* state = Playable();
  if (state == nullptr)
  {
    return true;
  }
  XAUDIO2_VOICE_STATE voiceState{};
  state->voice->GetState(&voiceState, XAUDIO2_VOICE_NOSAMPLESPLAYED);
  return voiceState.BuffersQueued == 0;
}

void Voice::Start()
{
  State* state = Playable();
  if (state == nullptr)
  {
    return;
  }
  state->started = true;
  state->device->Check(state->voice->Start(), "IXAudio2SourceVoice::Start");
}

void Voice::Stop()
{
  State* state = Playable();
  if (state == nullptr)
  {
    return;
  }
  state->started = false;
  state->device->Check(state->voice->Stop(), "IXAudio2SourceVoice::Stop");
}

void Voice::SetVolume(float _volume)
{
  const State* state = Playable();
  if (state == nullptr)
  {
    return;
  }
  state->device->Check(state->voice->SetVolume(_volume), "IXAudio2SourceVoice::SetVolume");
}

void Voice::SetPan(float _pan)
{
  const State* state = Playable();
  if (state == nullptr)
  {
    return;
  }
  AudioDevice::Native& device = *state->device;
  device.matrix.resize(static_cast<std::size_t>(state->buffer->Channels()) * device.outputChannels);
  if (PanMatrix(state->buffer->Channels(), device.outputChannels, _pan, device.matrix))
  {
    state->SetOutputMatrix();
  }
}

void Voice::SetFrequencyRatio(float _ratio)
{
  State* state = Playable();
  if (state == nullptr)
  {
    return;
  }
  state->frequencyRatio = _ratio;
  state->ApplyFrequency();
}

void Voice::Seek(std::uint32_t _frame)
{
  const State* state = Playable();
  if (state == nullptr)
  {
    return;
  }
  std::uint32_t frame = _frame - (_frame % state->buffer->FramesPerBlock());
  if (frame >= state->buffer->Frames())
  {
    frame = 0;
  }
  AudioDevice::Native& device = *state->device;
  if (!device.Check(state->voice->Stop(), "IXAudio2SourceVoice::Stop") ||
      !device.Check(state->voice->FlushSourceBuffers(), "IXAudio2SourceVoice::FlushSourceBuffers"))
  {
    return;
  }
  state->Submit(frame);
  if (state->started)
  {
    device.Check(state->voice->Start(), "IXAudio2SourceVoice::Start");
  }
}

void Voice::Spatialize(const Emitter& _emitter)
{
  State* state = Playable();
  if (state == nullptr || state->buffer->Channels() > MAX_EMITTER_CHANNELS)
  {
    return;
  }
  AudioDevice::Native& device = *state->device;
  const std::uint32_t inChannels = state->buffer->Channels();

  std::array<float, MAX_EMITTER_CHANNELS> azimuths{};
  X3DAUDIO_EMITTER emitter{};
  emitter.OrientFront.z = 1.0f;
  emitter.OrientTop.y = 1.0f;
  emitter.Position = ToVector(_emitter.position);
  emitter.Velocity = ToVector(_emitter.velocity);
  emitter.ChannelCount = inChannels;
  emitter.pChannelAzimuths = azimuths.data();
  emitter.CurveDistanceScaler = _emitter.minDistance > MIN_CURVE_DISTANCE ? _emitter.minDistance : MIN_CURVE_DISTANCE;
  emitter.DopplerScaler = 1.0f;

  device.matrix.assign(static_cast<std::size_t>(inChannels) * device.outputChannels, 0.0f);
  X3DAUDIO_DSP_SETTINGS dsp{};
  dsp.SrcChannelCount = inChannels;
  dsp.DstChannelCount = device.outputChannels;
  dsp.pMatrixCoefficients = device.matrix.data();
  X3DAudioCalculate(device.x3d, &device.listener, &emitter, X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER, &dsp);

  state->SetOutputMatrix();
  state->dopplerFactor = dsp.DopplerFactor;
  state->ApplyFrequency();
}

AudioDevice::AudioDevice(Desc _desc)
  : m_native(std::make_unique<Native>())
{
  Native& native = *m_native;
  native.desc = std::move(_desc);
  native.listener.OrientFront.z = 1.0f;
  native.listener.OrientTop.y = 1.0f;

  if (!native.Check(XAudio2Create(&native.xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR), "XAudio2Create"))
  {
    native.silent = true;
    return;
  }

  // A machine without an audio device is not a fault of the program: it runs, silently.
  IXAudio2MasteringVoice* master = nullptr;
  const HRESULT result = native.xaudio->CreateMasteringVoice(&master);
  if (FAILED(result))
  {
    native.GoSilent(result);
    return;
  }
  native.master.reset(master);

  XAUDIO2_VOICE_DETAILS details{};
  native.master->GetVoiceDetails(&details);
  native.outputChannels = details.InputChannels;

  DWORD channelMask = 0;
  if (!native.Check(native.master->GetChannelMask(&channelMask), "IXAudio2MasteringVoice::GetChannelMask") ||
      !native.Check(X3DAudioInitialize(channelMask, native.desc.speedOfSoundUnitsPerSecond, native.x3d), "X3DAudioInitialize"))
  {
    native.silent = true;
  }
}

AudioDevice::~AudioDevice()
{
  // A voice somebody still holds outlives the device, but not its XAudio2 voice.
  for (Voice::State* state : m_native->voices)
  {
    state->voice.reset();
    state->device = nullptr;
  }
  m_native->voices.clear();
  m_native->master.reset();
  m_native->xaudio.Reset();
}

bool AudioDevice::IsSilent() const noexcept
{
  return m_native->silent;
}

std::uint32_t AudioDevice::OutputChannels() const noexcept
{
  return m_native->outputChannels;
}

std::uint32_t AudioDevice::VoicesInUse() const noexcept
{
  return static_cast<std::uint32_t>(m_native->voices.size());
}

void AudioDevice::SetListener(const Listener& _listener) noexcept
{
  if (Length(_listener.front) <= 0.0f || Length(_listener.top) <= 0.0f)
  {
    return;
  }
  const Float3 front = Normalize(_listener.front);
  const float along = Dot(_listener.top, front);
  const Float3 top = {_listener.top.x - (along * front.x), _listener.top.y - (along * front.y), _listener.top.z - (along * front.z)};
  if (Length(top) <= 0.0f)
  {
    return;
  }
  m_native->listener.OrientFront = ToVector(front);
  m_native->listener.OrientTop = ToVector(Normalize(top));
  m_native->listener.Velocity = ToVector(_listener.velocity);
}

Voice AudioDevice::CreateVoice(std::shared_ptr<const SoundBuffer> _buffer, bool _looped, std::string_view _name)
{
  Native& native = *m_native;
  if (native.silent || !native.master || !_buffer || _buffer->Frames() == 0)
  {
    return {};
  }
  if (native.voices.size() >= native.desc.maxVoices)
  {
    Report(native.desc.onWarning, std::format("XAudio2: all voices are in use, so {} is not played", _name));
    return {};
  }

  IXAudio2SourceVoice* source = nullptr;
  const HRESULT result = native.xaudio->CreateSourceVoice(&source, reinterpret_cast<const WAVEFORMATEX*>(_buffer->FormatBytes().data()), 0,
                                                          native.desc.maxFrequencyRatio);
  if (result == XAUDIO2_E_DEVICE_INVALIDATED)
  {
    native.GoSilent(result);
    return {};
  }
  if (!native.Check(result, "IXAudio2::CreateSourceVoice", _name))
  {
    return {};
  }

  auto state = std::make_unique<Voice::State>();
  state->device = &native;
  state->buffer = std::move(_buffer);
  state->voice.reset(source);
  state->looped = _looped;
  native.voices.push_back(state.get());
  state->Submit(0);
  return Voice(std::move(state));
}

} // namespace Neuron
