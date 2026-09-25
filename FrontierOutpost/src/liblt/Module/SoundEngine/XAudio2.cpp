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

#include "AudioDevice.h"
#include "SoundBuffer.h"

#include <memory>
#include <span>
#include <string>

/* The sound engine, over NeuronClient's AudioDevice, which plays sounds with XAudio2 and places
   them with X3DAudio (ADR-003). NeuronClient has the mechanism: WAV parsing, the voices, FMOD Ex's
   pan law and X3DAudio's inverse rolloff. This file has the policy, which is FMOD's, as the FMOD
   engine configured it:
   - positions are relative to the camera. A 3D sound is not attenuated up to
     kDistanceScale * distanceDiv, and falls off as 1 / distance beyond, up to kMaxDistance;
   - the Doppler shift uses FMOD's speed of sound, and velocities are scaled by kVelocityScale;
   - SetPitch sets 44100 Hz times the pitch;
   - a failed call ends the program through the engine's assertion handler.
   Sounds are WAV files. A missing one plays silence, where FMOD's engine ended the program: the
   owner's conversion of the Ogg sounds can land when it is ready. XAudio2 has no virtual voices:
   every voice is mixed, up to kMaxVoices. */

const float kDistanceScale = 50.0f;
const float kVelocityScale = 1.0f / 50.0f;

/* FMOD Ex stopped attenuating at the maximum distance the FMOD engine gave every sound. */
const float kMaxDistance = 100000.0f;

/* The speed of sound FMOD Ex computed the Doppler shift with, in units per second. */
const float kSpeedOfSound = 340.0f;

/* FMOD mixed the 128 most audible of up to 1024 channels. */
const uint kMaxVoices = 1024;

/* The highest frequency ratio a voice accepts: pitch and Doppler shift together, against the
   sound's own sample rate. */
const float kMaxFrequencyRatio = 8.0f;

/* FMOD's setFrequency took hertz: SetPitch has always meant this many hertz times the pitch. */
const float kPitchFrequency = 44100.0f;

/* Samples handed to Play are mono, 32-bit float, at this rate. */
const uint kBufferSampleRate = 44100;

const String kDefaultSoundPath = "sound/";

namespace {
  typedef std::shared_ptr<Neuron::SoundBuffer const> SoundBufferPtr;

  Neuron::Float3 ToFloat3(V3 const& v) {
    Neuron::Float3 f = { v.x, v.y, v.z };
    return f;
  }

  struct SoundImpl : public SoundT {
    SoundBufferPtr data;
    Neuron::Voice voice;
    float volume;
    float pan;
    float pitch;
    bool deleted;
    bool looped;
    bool playing;
    bool spatialized;

    SoundImpl(SoundBufferPtr const& data) :
      data(data),
      volume(1),
      pan(0),
      pitch(1),
      deleted(false),
      looped(false),
      playing(false),
      spatialized(false)
      {}

    void Delete() {
      deleted = true;
    }

    bool IsFinished() const {
      return voice.IsFinished();
    }

    bool IsLooped() const {
      return looped;
    }

    float GetDuration() const {
      return voice ? data->DurationMs() : 0;
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

      uint length = (uint)data->DurationMs();
      if (!length) return;

      uint ms = (uint)position % length;
      voice.Seek((uint)((double)ms * data->SampleRateHz() / 1000.0));
    }

    void SetPan(float pan) {
      pan = Clamp(pan, -1.0f, 1.0f);
      this->pan = pan;
      if (spatialized) return;
      voice.SetPan(pan);
    }

    void SetPitch(float pitch) {
      this->pitch = pitch;
      voice.SetFrequencyRatio(kPitchFrequency * pitch / (float)data->SampleRateHz());
    }

    void SetPlaying(bool playing) {
      this->playing = playing;
      if (playing)
        voice.Start();
      else
        voice.Stop();
    }

    void SetVolume(float volume) {
      volume = Saturate(volume);
      this->volume = volume;
      voice.SetVolume(volume);
    }

    void ReleaseVoice() {
      voice = Neuron::Voice();
    }

    /* Relative to the listener at the origin. */
    void Spatialize(V3 const& position, V3 const& velocity, float minDistance) {
      if (!voice) return;

      V3 p = position;
      float distance = Length(p);
      if (distance > kMaxDistance)
        p = (kMaxDistance / distance) * p;

      Neuron::Emitter emitter;
      emitter.position = ToFloat3(p);
      emitter.velocity = ToFloat3(velocity);
      emitter.minDistance = minDistance;
      voice.Spatialize(emitter);
    }
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

  Neuron::AudioDevice::Desc DeviceDesc() {
    Neuron::AudioDevice::Desc desc;
    desc.speedOfSoundUnitsPerSecond = kSpeedOfSound;
    desc.maxVoices = kMaxVoices;
    desc.maxFrequencyRatio = kMaxFrequencyRatio;
    desc.onFailure = [](std::string const& message) {
      LTE_ASSERT_FAILURE(__FILE__, __LINE__, message.c_str());
    };
    desc.onWarning = [](std::string const& message) {
      Log_Warning(message);
    };
    return desc;
  }

  struct SoundEngineXAudio2Impl : public SoundEngine {
    /* First, so that it goes last: a sound somebody still holds outlives the engine, but not its
       voice. */
    Neuron::AudioDevice device;
    V3D camPos;

    Map<String, SoundBufferPtr> sources;

    Vector<Sound2DInstance*> sounds2D;
    Vector<Sound3DInstance*> sounds3D;

    SoundEngineXAudio2Impl() :
      device(DeviceDesc()),
      camPos(0)
      {}

    ~SoundEngineXAudio2Impl() {
      for (size_t i = 0; i < sounds2D.size(); ++i)
        delete sounds2D[i];
      for (size_t i = 0; i < sounds3D.size(); ++i)
        delete sounds3D[i];
    }

    char const* GetName() const {
      return "SoundEngine (XAudio2)";
    }

    void Update() {
      SFRAME("Sound Engine");

      FRAME("Listener Update") {
        /* Update the sound engine's listener location. */
        Camera const& camera = Camera_Get();
        if (camera) {
          camPos = camera->GetPos();
          V3 camVelocity = camera->GetTarget()
            ? kVelocityScale * camera->GetTarget()->GetVelocity()
            : V3(0);

          Neuron::Listener listener;
          listener.front = ToFloat3(camera->GetLook());
          listener.top = ToFloat3(camera->GetUp());
          listener.velocity = ToFloat3(camVelocity);
          device.SetListener(listener);
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

    /* A missing file is named once, as a warning, and has no samples, so its sounds play silence.
       A file that is there but that XAudio2 cannot play is a broken asset, and ends the program. */
    SoundBufferPtr GetSource(String const& file) {
      String path = kDefaultSoundPath + file;
      if (!sources.contains(path)) {
        Location location = Location_Resource(path);
        SoundBufferPtr source;
        if (!location->Exists()) {
          Log_Warning("XAudio2: " + path + " is missing, so it plays silence");
          source = std::make_shared<Neuron::SoundBuffer const>(
            Neuron::SoundBuffer::FromSamples(std::span<float const>(), kBufferSampleRate));
        } else {
          AutoPtr< Array<uchar> > arr = location->Read();
          if (!arr)
            Log_Critical("Failed to read sound " + file);

          Neuron::SoundBuffer buffer;
          std::string reason;
          if (!Neuron::SoundBuffer::FromWav(
                std::as_bytes(std::span<uchar const>(arr->data(), arr->size())), buffer, reason))
          {
            String message =
              "XAudio2: " + path + " is not a WAV file XAudio2 can play: " + reason;
            LTE_ASSERT_FAILURE(__FILE__, __LINE__, message.c_str());
          }
          source = std::make_shared<Neuron::SoundBuffer const>(std::move(buffer));
        }
        sources[path] = source;
      }
      return sources[path];
    }

    /* A new voice, stopped, with the sound queued on it. Without a device, a free voice or any
       samples, the sound is created finished. */
    SoundImpl* CreateSound(SoundBufferPtr const& data, String const& name, bool looped) {
      SoundImpl* s = new SoundImpl(data);
      s->looped = looped;
      s->voice = device.CreateVoice(data, looped, name);
      return s;
    }

    Sound Play(Array<float> const& buffer) {
      SoundBufferPtr data = std::make_shared<Neuron::SoundBuffer const>(
        Neuron::SoundBuffer::FromSamples(
          std::span<float const>(buffer.data(), buffer.size()), kBufferSampleRate));

      Sound2DInstance* info = new Sound2DInstance;
      SoundImpl* s = CreateSound(data, "a sample buffer", false);
      info->sound = s;
      s->voice.SetPan(s->pan);
      info->sound->SetPlaying(true);
      sounds2D << info;
      return s;
    }

    Sound Play2D(String const& name, float volume, bool looped) {
      Sound2DInstance* info = new Sound2DInstance;
      info->sound = CreateSound(GetSource(name), name, looped);
      info->sound->voice.SetPan(info->sound->pan);
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
}

SoundEngine* SoundEngine_XAudio2() {
  return new SoundEngineXAudio2Impl;
}
