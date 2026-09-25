#include "Module/SoundEngine.h"

#include "Component/Orientation.h"

#include "Game/Camera.h"
#include "Game/Object.h"

#include "LTE/Array.h"
#include "LTE/AutoPtr.h"
#include "LTE/Location.h"
#include "LTE/Map.h"
#include "LTE/Math.h"
#include "LTE/Pool.h"
#include "LTE/ProgramLog.h"
#include "LTE/StackFrame.h"
#include "LTE/V3.h"
#include "LTE/Vector.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>

/* The engine's error() macro would rewrite any error(...) in the SDK headers, so it is set aside
   while they are read. */
#pragma push_macro("error")
#undef error
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>
#include <x3daudio.h>
#include <wrl/client.h>
#pragma pop_macro("error")

/* XAudio2 and X3DAudio, from the Windows SDK, in place of FMOD Ex (ADR-003). The behaviour is
   FMOD's, as the FMOD engine configured it:
   - positions are relative to the camera. A 3D sound is not attenuated up to
     kDistanceScale * distanceDiv, and falls off as 1 / distance beyond, up to kMaxDistance
     (FMOD's inverse rolloff);
   - the Doppler shift uses FMOD's speed of sound, and velocities are scaled by kVelocityScale;
   - a 2D sound is panned by FMOD Ex's pan law, and SetPitch sets 44100 Hz times the pitch;
   - a failed call ends the program through the engine's assertion handler.
   Sounds are WAV files. XAudio2 has no virtual voices: every voice is mixed, up to kMaxVoices. */

const float kDistanceScale = 50.0f;
const float kVelocityScale = 1.0f / 50.0f;

/* FMOD Ex stopped attenuating at the maximum distance the FMOD engine gave every sound. */
const float kMaxDistance = 100000.0f;

/* The speed of sound FMOD Ex computed the Doppler shift with, in units per second. */
const float kSpeedOfSound = 340.0f;

/* FMOD mixed the 128 most audible of up to 1024 channels. */
const size_t kMaxVoices = 1024;

/* The highest frequency ratio a voice accepts: pitch and Doppler shift together, against the
   sound's own sample rate. */
const float kMaxFrequencyRatio = 8.0f;

/* FMOD's setFrequency took hertz: SetPitch has always meant this many hertz times the pitch. */
const float kPitchFrequency = 44100.0f;

/* Channels a 3D sound may have: X3DAudio places each of them at the emitter. */
const uint kMaxEmitterChannels = 8;

const String kDefaultSoundPath = "sound/";

namespace {
  struct SoundEngineXAudio2Impl;

  void CheckResult(HRESULT result, char const* call, String const& subject = "") {
    if (SUCCEEDED(result))
      return;

    std::stringstream stream;
    stream << "XAudio2: " << call << " failed with 0x"
           << std::hex << (unsigned long)result;
    if (subject.size())
      stream << " for " << subject.c_str();
    LTE_ASSERT_FAILURE(__FILE__, __LINE__, stream.str().c_str());
  }

  /* A voice is not reference-counted: it is destroyed once, by DestroyVoice. */
  struct VoiceDeleter {
    void operator()(IXAudio2Voice* voice) const {
      voice->DestroyVoice();
    }
  };

  typedef std::unique_ptr<IXAudio2SourceVoice, VoiceDeleter> SourceVoicePtr;
  typedef std::unique_ptr<IXAudio2MasteringVoice, VoiceDeleter> MasteringVoicePtr;

  /* XAudio2 wants COM initialized on the thread that creates it. */
  struct ComScope {
    bool initialized;

    ComScope() :
      initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
      {}

    ~ComScope() {
      if (initialized)
        CoUninitialize();
    }
  };

  /* A sound's format and samples. A file is read once and shared by every voice that plays it. */
  struct SoundData {
    Array<uchar> format;
    Array<uchar> samples;
    uint frames;
    uint framesPerBlock;

    SoundData() :
      frames(0),
      framesPerBlock(1)
      {}

    WAVEFORMATEX const* GetFormat() const {
      return (WAVEFORMATEX const*)format.data();
    }

    float GetDurationMs() const {
      return 1000.0f * (float)frames / (float)GetFormat()->nSamplesPerSec;
    }

    /* Reads a RIFF WAVE file. XAudio2 plays PCM, IEEE float and MS-ADPCM, and refuses anything
       else when the voice is created. */
    bool Read(Array<uchar> const& file) {
      uchar const* p = file.data();
      size_t size = file.size();
      if (size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
        return false;

      uchar const* fmt = nullptr;
      uchar const* data = nullptr;
      uint fmtSize = 0;
      uint dataSize = 0;
      for (size_t at = 12; at + 8 <= size;) {
        uint chunkSize;
        memcpy(&chunkSize, p + at + 4, 4);
        size_t available = size - (at + 8);
        uint length = chunkSize < available ? chunkSize : (uint)available;
        if (!memcmp(p + at, "fmt ", 4)) {
          fmt = p + at + 8;
          fmtSize = length;
        } else if (!memcmp(p + at, "data", 4)) {
          data = p + at + 8;
          dataSize = length;
        }
        at += 8 + (size_t)chunkSize + (chunkSize & 1);
      }

      if (!fmt || !data || fmtSize < 16)
        return false;

      /* Copied rather than pointed into, so that it is aligned, and so that a 16-byte PCM chunk
         reads as a WAVEFORMATEX with cbSize 0. */
      size_t formatSize = fmtSize < sizeof(WAVEFORMATEX) ? sizeof(WAVEFORMATEX) : fmtSize;
      format = Array<uchar>(formatSize, (uchar)0);
      memcpy(format.data(), fmt, fmtSize);

      WAVEFORMATEX const* wf = GetFormat();
      if (!wf->nBlockAlign || !wf->nSamplesPerSec || !wf->nChannels)
        return false;

      if (wf->wFormatTag == WAVE_FORMAT_ADPCM && fmtSize >= 20) {
        WORD samplesPerBlock;
        memcpy(&samplesPerBlock, fmt + 18, 2);
        framesPerBlock = samplesPerBlock ? samplesPerBlock : 1;
      }

      /* XAudio2 takes whole blocks only. */
      uint blocks = dataSize / wf->nBlockAlign;
      if (!blocks)
        return false;
      samples = Array<uchar>(blocks * wf->nBlockAlign, data);
      frames = blocks * framesPerBlock;
      return true;
    }

    /* Samples handed over by the caller: mono, 32-bit float, at 44100 Hz. */
    void Set(Array<float> const& buffer) {
      format = Array<uchar>(sizeof(WAVEFORMATEX), (uchar)0);
      WAVEFORMATEX* wf = (WAVEFORMATEX*)format.data();
      wf->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
      wf->nChannels = 1;
      wf->nSamplesPerSec = 44100;
      wf->wBitsPerSample = 32;
      wf->nBlockAlign = (WORD)sizeof(float);
      wf->nAvgBytesPerSec = wf->nSamplesPerSec * wf->nBlockAlign;
      samples = Array<uchar>(buffer.size() * sizeof(float), (uchar const*)buffer.data());
      frames = (uint)buffer.size();
    }
  };

  struct SoundImpl : public SoundT {
    SoundEngineXAudio2Impl* engine;
    SoundData const* data;
    std::unique_ptr<SoundData> ownData;
    SourceVoicePtr voice;
    float volume;
    float pan;
    float pitch;
    float frequencyRatio;
    float dopplerFactor;
    bool deleted;
    bool looped;
    bool playing;
    bool spatialized;

    SoundImpl(SoundEngineXAudio2Impl* engine, SoundData const* data) :
      engine(engine),
      data(data),
      volume(1),
      pan(0),
      pitch(1),
      frequencyRatio(1),
      dopplerFactor(1),
      deleted(false),
      looped(false),
      playing(false),
      spatialized(false)
      {}

    ~SoundImpl();

    void Delete() {
      deleted = true;
    }

    bool IsFinished() const {
      if (!voice)
        return true;

      XAUDIO2_VOICE_STATE state;
      voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
      return state.BuffersQueued == 0;
    }

    bool IsLooped() const {
      return looped;
    }

    float GetDuration() const {
      return voice ? data->GetDurationMs() : 0;
    }

    float GetPan() const {
      return pan;
    }

    float GetPitch() const {
      return pitch;
    }

    float GetVolume() const {
      return volume;
    }

    void SetCursor(float position) {
      if (!voice) return;

      uint length = (uint)data->GetDurationMs();
      if (!length) return;

      uint ms = (uint)position % length;
      uint frame = (uint)((double)ms * data->GetFormat()->nSamplesPerSec / 1000.0);
      frame -= frame % data->framesPerBlock;
      if (frame >= data->frames)
        frame = 0;

      /* Applied in call order: the voice stops, drops what it had queued, and starts again from
         the new position. */
      CheckResult(voice->Stop(), "IXAudio2SourceVoice::Stop");
      CheckResult(voice->FlushSourceBuffers(), "IXAudio2SourceVoice::FlushSourceBuffers");
      Submit(frame);
      if (playing)
        CheckResult(voice->Start(), "IXAudio2SourceVoice::Start");
    }

    void SetPan(float pan) {
      pan = Clamp(pan, -1.0f, 1.0f);
      this->pan = pan;
      if (!voice || spatialized) return;
      ApplyPan();
    }

    void SetPitch(float pitch) {
      this->pitch = pitch;
      frequencyRatio =
        kPitchFrequency * pitch / (float)data->GetFormat()->nSamplesPerSec;
      if (!voice) return;
      ApplyFrequency();
    }

    void SetPlaying(bool playing) {
      this->playing = playing;
      if (!voice) return;

      if (playing)
        CheckResult(voice->Start(), "IXAudio2SourceVoice::Start");
      else
        CheckResult(voice->Stop(), "IXAudio2SourceVoice::Stop");
    }

    void SetVolume(float volume) {
      volume = Saturate(volume);
      this->volume = volume;
      if (!voice) return;
      CheckResult(voice->SetVolume(volume), "IXAudio2SourceVoice::SetVolume");
    }

    /* Queues the sound from 'frame' to its end, then all of it over and over if it loops. A
       buffer that starts part-way must say how long it is, and may not loop back to before its
       start, so a looping sound started part-way is two buffers: the rest of it once, then all of
       it in a loop. */
    void Submit(uint frame) {
      if (frame > 0) {
        XAUDIO2_BUFFER rest;
        memset(&rest, 0, sizeof(rest));
        rest.AudioBytes = (UINT32)data->samples.size();
        rest.pAudioData = data->samples.data();
        rest.PlayBegin = frame;
        rest.PlayLength = data->frames - frame;
        if (!looped)
          rest.Flags = XAUDIO2_END_OF_STREAM;
        CheckResult(voice->SubmitSourceBuffer(&rest), "IXAudio2SourceVoice::SubmitSourceBuffer");
        if (!looped)
          return;
      }

      XAUDIO2_BUFFER all;
      memset(&all, 0, sizeof(all));
      all.Flags = XAUDIO2_END_OF_STREAM;
      all.AudioBytes = (UINT32)data->samples.size();
      all.pAudioData = data->samples.data();
      if (looped)
        all.LoopCount = XAUDIO2_LOOP_INFINITE;
      CheckResult(voice->SubmitSourceBuffer(&all), "IXAudio2SourceVoice::SubmitSourceBuffer");
    }

    void ApplyFrequency() {
      float ratio = Clamp(
        frequencyRatio * dopplerFactor, XAUDIO2_MIN_FREQ_RATIO, kMaxFrequencyRatio);
      CheckResult(voice->SetFrequencyRatio(ratio), "IXAudio2SourceVoice::SetFrequencyRatio");
    }

    void ApplyPan();
    void ReleaseVoice();
    void Spatialize(V3 const& position, V3 const& velocity, float minDistance);
  };

  struct Sound2DInstance {
    Reference<SoundImpl> sound;
    typedef Sound2DInstance SelfType;

    POOLED_TYPE
  };

  struct Sound3DInstance {
    Reference<SoundImpl> sound;
    Object carrier;
    V3 offset;
    float minDistance;

    typedef Sound3DInstance SelfType;
    POOLED_TYPE

    void Update(V3D const& camPos) {
      V3D position = carrier
        ? (V3D)carrier->GetTransform().TransformPoint(offset)
        : (V3D)offset;

      V3 relative = (V3)(position - camPos);
      V3 velocity = carrier
        ? kVelocityScale * carrier->GetVelocity()
        : 0;

      sound->Spatialize(relative, velocity, minDistance);
    }
  };

  struct SoundEngineXAudio2Impl : public SoundEngine {
    ComScope com;
    Microsoft::WRL::ComPtr<IXAudio2> xaudio;
    MasteringVoicePtr master;
    X3DAUDIO_HANDLE x3d;
    X3DAUDIO_LISTENER listener;
    uint outputChannels;
    size_t voices;
    bool silent;
    V3D camPos;
    Vector<float> matrix;

    Map<String, SoundData*> sources;
    Vector<SoundImpl*> live;

    Vector<Sound2DInstance*> sounds2D;
    Vector<Sound3DInstance*> sounds3D;

    SoundEngineXAudio2Impl() :
      outputChannels(0),
      voices(0),
      silent(false),
      camPos(0)
    {
      memset(x3d, 0, sizeof(x3d));
      memset(&listener, 0, sizeof(listener));
      listener.OrientFront.z = 1;
      listener.OrientTop.y = 1;

      CheckResult(XAudio2Create(xaudio.GetAddressOf()), "XAudio2Create");

      /* A machine without an audio device is not a fault of the program: it runs, silently. */
      IXAudio2MasteringVoice* mastering = nullptr;
      HRESULT result = xaudio->CreateMasteringVoice(&mastering);
      if (FAILED(result)) {
        GoSilent(result);
        return;
      }
      master.reset(mastering);

      XAUDIO2_VOICE_DETAILS details;
      master->GetVoiceDetails(&details);
      outputChannels = details.InputChannels;

      DWORD channelMask;
      CheckResult(master->GetChannelMask(&channelMask), "IXAudio2MasteringVoice::GetChannelMask");
      CheckResult(X3DAudioInitialize(channelMask, kSpeedOfSound, x3d), "X3DAudioInitialize");
    }

    ~SoundEngineXAudio2Impl() {
      for (size_t i = 0; i < sounds2D.size(); ++i)
        delete sounds2D[i];
      for (size_t i = 0; i < sounds3D.size(); ++i)
        delete sounds3D[i];

      /* A sound somebody still holds outlives the engine, but not its voice. */
      for (size_t i = 0; i < live.size(); ++i) {
        live[i]->voice.reset();
        live[i]->engine = nullptr;
      }

      master.reset();
      xaudio.Reset();

      for (Map<String, SoundData*>::iterator it = sources.begin(); it != sources.end(); ++it)
        delete it->second;
    }

    char const* GetName() const {
      return "SoundEngine (XAudio2)";
    }

    void GoSilent(HRESULT result) {
      if (silent) return;
      silent = true;

      std::stringstream stream;
      stream << "XAudio2: no audio device (0x" << std::hex << (unsigned long)result
             << "), so there is no sound";
      Log_Warning(stream.str());
    }

    void Update() {
      SFRAME("Sound Engine");

      FRAME("Listener Update") {
        /* Update the sound engine's listener location. */
        Camera const& camera = Camera_Get();
        if (camera) {
          camPos = camera->GetPos();
          V3 camLook = camera->GetLook();
          V3 camUp = camera->GetUp();

          V3 camVelocity = camera->GetTarget()
            ? kVelocityScale * camera->GetTarget()->GetVelocity()
            : V3(0);

          SetListener(camLook, camUp, camVelocity);
        }
      }

      FRAME("2D Sound Update") {
        for (int i = 0; i < (int)sounds2D.size(); ++i) {
          Sound2DInstance* info = sounds2D[i];
          bool finished = info->sound->IsFinished();
          if (finished || info->sound->deleted) {
            if (finished)
              info->sound->ReleaseVoice();
            info->sound->deleted = false;
            delete info;
            sounds2D.removeIndex(i);
            i--;
            continue;
          }
        }
      }

      FRAME("3D Sound Update") {
        for (int i = 0; i < (int)sounds3D.size(); ++i) {
          Sound3DInstance* info = sounds3D[i];
          bool finished = info->sound->IsFinished();
          if ((info->carrier && info->carrier->IsDeleted())
              || finished
              || info->sound->deleted)
          {
            if (finished)
              info->sound->ReleaseVoice();
            info->sound->deleted = false;
            delete info;
            sounds3D.removeIndex(i);
            i--;
            continue;
          }

          info->Update(camPos);
        }
      }
    }

    /* X3DAudio wants an orthonormal listener basis; FMOD Ex wanted the same of its caller. */
    void SetListener(V3 const& look, V3 const& up, V3 const& velocity) {
      if (Length(look) <= 0 || Length(up) <= 0)
        return;

      V3 front = Normalize(look);
      V3 top = up - Dot(up, front) * front;
      if (Length(top) <= 0)
        return;
      top = Normalize(top);

      listener.OrientFront.x = front.x;
      listener.OrientFront.y = front.y;
      listener.OrientFront.z = front.z;
      listener.OrientTop.x = top.x;
      listener.OrientTop.y = top.y;
      listener.OrientTop.z = top.z;
      listener.Velocity.x = velocity.x;
      listener.Velocity.y = velocity.y;
      listener.Velocity.z = velocity.z;
    }

    SoundData const* GetSource(String const& file) {
      String path = kDefaultSoundPath + file;
      if (!sources.contains(path)) {
        AutoPtr< Array<uchar> > arr = Location_Resource(path)->Read();
        if (!arr)
          Log_Critical("Failed to read sound " + file);

        SoundData* source = new SoundData;
        if (!source->Read(*arr)) {
          std::stringstream stream;
          stream << "XAudio2: " << path.c_str() << " is not a WAV file XAudio2 can play";
          LTE_ASSERT_FAILURE(__FILE__, __LINE__, stream.str().c_str());
        }
        sources[path] = source;
      }
      return sources[path];
    }

    /* A new voice, stopped, with the sound queued on it. Without a device or a free voice, the
       sound is created finished. */
    SoundImpl* CreateSound(SoundData const* data, String const& name, bool looped) {
      SoundImpl* s = new SoundImpl(this, data);
      s->looped = looped;
      live << s;
      if (silent || !master)
        return s;

      if (voices >= kMaxVoices) {
        Log_Warning("XAudio2: all voices are in use, so " + name + " is not played");
        return s;
      }

      IXAudio2SourceVoice* voice = nullptr;
      HRESULT result = xaudio->CreateSourceVoice(
        &voice, data->GetFormat(), 0, kMaxFrequencyRatio);
      if (result == XAUDIO2_E_DEVICE_INVALIDATED) {
        GoSilent(result);
        return s;
      }
      CheckResult(result, "IXAudio2::CreateSourceVoice", name);

      s->voice.reset(voice);
      voices++;
      s->Submit(0);
      return s;
    }

    Sound Play(Array<float> const& buffer) {
      SoundData* data = new SoundData;
      data->Set(buffer);

      Sound2DInstance* info = new Sound2DInstance;
      SoundImpl* s = CreateSound(data, "a sample buffer", false);
      s->ownData.reset(data);

      info->sound = s;
      if (s->voice)
        s->ApplyPan();
      info->sound->SetPlaying(true);
      sounds2D << info;
      return s;
    }

    Sound Play2D(String const& name, float volume, bool looped) {
      Sound2DInstance* info = new Sound2DInstance;
      info->sound = CreateSound(GetSource(name), name, looped);
      if (info->sound->voice)
        info->sound->ApplyPan();
      info->sound->SetVolume(volume);
      info->sound->SetPlaying(true);
      sounds2D << info;
      return info->sound;
    }

    Sound Play3D(
      String const& name,
      Object const& carrier,
      V3 const& offset,
      float volume,
      float distanceDiv,
      bool looped)
    {
      Sound3DInstance* info = new Sound3DInstance;
      info->carrier = carrier;
      info->offset = offset;
      info->minDistance = kDistanceScale * distanceDiv;
      info->sound = CreateSound(GetSource(name), name, looped);
      info->sound->spatialized = true;
      info->sound->SetVolume(volume);
      info->Update(camPos);
      info->sound->SetPlaying(true);
      sounds3D << info;
      return info->sound;
    }
  };

  SoundImpl::~SoundImpl() {
    if (engine) {
      ReleaseVoice();
      engine->live.remove(this);
    }
  }

  void SoundImpl::ReleaseVoice() {
    if (!voice) return;
    voice.reset();
    engine->voices--;
  }

  /* FMOD Ex's pan law. A mono sound is panned at constant power, so when centred it plays at 71%
     in both speakers. A stereo sound has one side faded down towards the other. */
  void SoundImpl::ApplyPan() {
    uint in = data->GetFormat()->nChannels;
    uint out = engine->outputChannels;
    if (in > 2 || !out)
      return;

    Vector<float>& m = engine->matrix;
    m.clear();
    for (uint i = 0; i < in * out; ++i)
      m << 0.0f;

    /* Element [destination * in + source]; destinations 0 and 1 are the front left and right. */
    if (out == 1) {
      for (uint i = 0; i < in; ++i)
        m[i] = 1.0f / (float)in;
    } else if (in == 1) {
      float angle = (pan + 1.0f) * 0.25f * 3.14159265358979f;
      m[0] = std::cos(angle);
      m[1] = std::sin(angle);
    } else {
      m[0] = pan <= 0 ? 1.0f : 1.0f - pan;
      m[3] = pan >= 0 ? 1.0f : 1.0f + pan;
    }

    CheckResult(voice->SetOutputMatrix(engine->master.get(), in, out, m.data()),
      "IXAudio2SourceVoice::SetOutputMatrix");
  }

  /* Direction, distance and Doppler shift, relative to the listener at the origin. */
  void SoundImpl::Spatialize(V3 const& position, V3 const& velocity, float minDistance) {
    uint in = data->GetFormat()->nChannels;
    if (!voice || in > kMaxEmitterChannels)
      return;

    V3 p = position;
    float distance = Length(p);
    if (distance > kMaxDistance)
      p = (kMaxDistance / distance) * p;

    float azimuths[kMaxEmitterChannels] = {};

    X3DAUDIO_EMITTER emitter;
    memset(&emitter, 0, sizeof(emitter));
    emitter.OrientFront.z = 1;
    emitter.OrientTop.y = 1;
    emitter.Position.x = p.x;
    emitter.Position.y = p.y;
    emitter.Position.z = p.z;
    emitter.Velocity.x = velocity.x;
    emitter.Velocity.y = velocity.y;
    emitter.Velocity.z = velocity.z;
    emitter.ChannelCount = in;
    emitter.pChannelAzimuths = azimuths;
    emitter.CurveDistanceScaler = minDistance > 1e-3f ? minDistance : 1e-3f;
    emitter.DopplerScaler = 1;

    uint out = engine->outputChannels;
    Vector<float>& m = engine->matrix;
    m.clear();
    for (uint i = 0; i < in * out; ++i)
      m << 0.0f;

    X3DAUDIO_DSP_SETTINGS dsp;
    memset(&dsp, 0, sizeof(dsp));
    dsp.SrcChannelCount = in;
    dsp.DstChannelCount = out;
    dsp.pMatrixCoefficients = m.data();

    X3DAudioCalculate(
      engine->x3d,
      &engine->listener,
      &emitter,
      X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER,
      &dsp);

    CheckResult(voice->SetOutputMatrix(engine->master.get(), in, out, m.data()),
      "IXAudio2SourceVoice::SetOutputMatrix");
    dopplerFactor = dsp.DopplerFactor;
    ApplyFrequency();
  }
}

SoundEngine* SoundEngine_XAudio2() {
  return new SoundEngineXAudio2Impl;
}
