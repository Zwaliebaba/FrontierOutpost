// Tests/NeuronClientTests/AudioOutput.cpp
//
// Neuron::PanMatrix is FMOD Ex's pan law, and Neuron::AudioDevice makes voices, or none without an
// audio device (Design/ADR/ADR-003). CI's runner has no audio device, so there the device tests
// take the silent path; on a machine with one, they take the other.
#include "pch.h"

#include "AudioDevice.h"
#include "Check.h"
#include "SoundBuffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

constexpr float TOLERANCE = 1e-6f;
constexpr float CENTERED = 0.70710678f; // cos(pi / 4): half the power on each side

/// What the device reported.
struct Messages
{
  std::vector<std::string> failures;
  std::vector<std::string> warnings;
};

/// FMOD's settings, as liblt passes them.
Neuron::AudioDevice::Desc DescFor(Messages& _messages, std::uint32_t _maxVoices = 16)
{
  return {.speedOfSoundUnitsPerSecond = 340.0f,
          .maxVoices = _maxVoices,
          .maxFrequencyRatio = 8.0f,
          .onFailure = [&_messages](const std::string& _message) { _messages.failures.push_back(_message); },
          .onWarning = [&_messages](const std::string& _message) { _messages.warnings.push_back(_message); }};
}

/// A tenth of a second of mono silence.
std::shared_ptr<const Neuron::SoundBuffer> Silence()
{
  return std::make_shared<const Neuron::SoundBuffer>(Neuron::SoundBuffer::FromSamples(std::vector<float>(4410, 0.0f), 44100));
}

void AssertNoFailure(const Messages& _messages)
{
  Assert::IsTrue(_messages.failures.empty(), _messages.failures.empty() ? L"" : Widen(_messages.failures.front()).c_str());
}

void AssertNear(float _expected, float _actual)
{
  Assert::AreEqual(_expected, _actual, TOLERANCE);
}

} // namespace

TEST_CLASS(AudioOutput)
{
public:
  TEST_METHOD(PansMonoAtConstantPower)
  {
    std::array<float, 2> matrix{};
    Assert::IsTrue(Neuron::PanMatrix(1, 2, 0.0f, matrix));
    AssertNear(CENTERED, matrix[0]);
    AssertNear(CENTERED, matrix[1]);

    Assert::IsTrue(Neuron::PanMatrix(1, 2, -1.0f, matrix));
    AssertNear(1.0f, matrix[0]);
    AssertNear(0.0f, matrix[1]);

    Assert::IsTrue(Neuron::PanMatrix(1, 2, 0.3f, matrix));
    AssertNear(1.0f, (matrix[0] * matrix[0]) + (matrix[1] * matrix[1]));
    Assert::IsTrue(matrix[1] > matrix[0], L"a pan to the right is louder on the right");
  }

  TEST_METHOD(PansStereoByFadingOneSide)
  {
    std::array<float, 4> matrix{};
    Assert::IsTrue(Neuron::PanMatrix(2, 2, 0.5f, matrix));
    AssertNear(0.5f, matrix[0]);
    AssertNear(0.0f, matrix[1]);
    AssertNear(0.0f, matrix[2]);
    AssertNear(1.0f, matrix[3]);

    Assert::IsTrue(Neuron::PanMatrix(2, 2, -0.25f, matrix));
    AssertNear(1.0f, matrix[0]);
    AssertNear(0.75f, matrix[3]);
  }

  TEST_METHOD(MixesIntoOneOutput)
  {
    std::array<float, 2> matrix{};
    Assert::IsTrue(Neuron::PanMatrix(2, 1, 0.7f, matrix));
    AssertNear(0.5f, matrix[0]);
    AssertNear(0.5f, matrix[1]);
  }

  TEST_METHOD(LeavesOtherOutputsSilent)
  {
    std::array<float, 6> matrix{};
    matrix.fill(9.0f);
    Assert::IsTrue(Neuron::PanMatrix(1, 6, 0.0f, matrix));
    AssertNear(CENTERED, matrix[0]);
    AssertNear(CENTERED, matrix[1]);
    for (std::size_t output = 2; output < matrix.size(); ++output)
    {
      AssertNear(0.0f, matrix[output]);
    }
  }

  TEST_METHOD(HoldsThePanToItsRange)
  {
    std::array<float, 2> beyond{};
    std::array<float, 2> right{};
    Assert::IsTrue(Neuron::PanMatrix(1, 2, 5.0f, beyond));
    Assert::IsTrue(Neuron::PanMatrix(1, 2, 1.0f, right));
    AssertNear(right[0], beyond[0]);
    AssertNear(right[1], beyond[1]);
  }

  TEST_METHOD(RefusesWhatItCannotPan)
  {
    std::array<float, 6> matrix{};
    matrix.fill(9.0f);
    Assert::IsFalse(Neuron::PanMatrix(3, 2, 0.0f, matrix));
    Assert::IsFalse(Neuron::PanMatrix(1, 0, 0.0f, matrix));
    Assert::IsFalse(Neuron::PanMatrix(2, 2, 0.0f, std::span(matrix).first(3)));
    for (const float element : matrix)
    {
      AssertNear(9.0f, element);
    }
  }

  TEST_METHOD(MakesVoicesOrIsSilent)
  {
    Messages messages;
    Neuron::AudioDevice device(DescFor(messages));
    AssertNoFailure(messages);
    Neuron::Voice voice = device.CreateVoice(Silence(), false, "silence");
    if (device.IsSilent())
    {
      Assert::AreEqual(std::size_t{1}, messages.warnings.size());
      Assert::IsTrue(messages.warnings.front().starts_with("XAudio2: no audio device"), Widen(messages.warnings.front()).c_str());
      Assert::IsFalse(static_cast<bool>(voice));
      Assert::IsTrue(voice.IsFinished());
      Assert::AreEqual(0u, device.VoicesInUse());
      return;
    }
    Assert::IsTrue(device.OutputChannels() >= 1);
    Assert::IsTrue(static_cast<bool>(voice));
    Assert::IsFalse(voice.IsFinished(), L"a stopped voice still has its sound queued");
    Assert::AreEqual(1u, device.VoicesInUse());
    voice = Neuron::Voice();
    Assert::AreEqual(0u, device.VoicesInUse());
    Assert::IsTrue(messages.warnings.empty());
  }

  TEST_METHOD(DrivesAVoiceWithoutAFailure)
  {
    Messages messages;
    Neuron::AudioDevice device(DescFor(messages));
    device.SetListener({.front = {0.0f, 0.0f, 2.0f}, .top = {0.0f, 1.0f, 1.0f}, .velocity = {1.0f, 0.0f, 0.0f}});
    Neuron::Voice voice = device.CreateVoice(Silence(), true, "loop");
    voice.SetVolume(0.5f);
    voice.SetPan(-0.5f);
    voice.SetFrequencyRatio(1.5f);
    voice.Start();
    voice.Seek(1000);
    voice.Spatialize({.position = {3.0f, 0.0f, 4.0f}, .velocity = {0.0f, 0.0f, -10.0f}, .minDistance = 2.0f});
    voice.Stop();
    AssertNoFailure(messages);
    Assert::IsTrue(device.IsSilent() || !voice.IsFinished(), L"a looped voice does not finish");
  }

  TEST_METHOD(MakesNoVoiceForASoundWithoutSamples)
  {
    Messages messages;
    Neuron::AudioDevice device(DescFor(messages));
    const std::size_t warnings = messages.warnings.size();
    const Neuron::Voice voice =
      device.CreateVoice(std::make_shared<const Neuron::SoundBuffer>(Neuron::SoundBuffer::FromSamples({}, 44100)), false, "nothing");
    Assert::IsFalse(static_cast<bool>(voice));
    Assert::IsTrue(voice.IsFinished());
    Assert::AreEqual(warnings, messages.warnings.size(), L"a sound without samples is not a fault");
    AssertNoFailure(messages);
  }

  TEST_METHOD(KeepsToItsVoiceLimit)
  {
    Messages messages;
    Neuron::AudioDevice device(DescFor(messages, 1));
    const std::shared_ptr<const Neuron::SoundBuffer> sound = Silence();
    Neuron::Voice first = device.CreateVoice(sound, false, "first");
    const Neuron::Voice second = device.CreateVoice(sound, false, "second");
    Assert::IsFalse(static_cast<bool>(second));
    if (device.IsSilent())
    {
      Assert::IsFalse(static_cast<bool>(first));
      return;
    }
    Assert::IsTrue(static_cast<bool>(first));
    Assert::IsTrue(messages.warnings.back() == "XAudio2: all voices are in use, so second is not played",
                   Widen(messages.warnings.back()).c_str());
    first = Neuron::Voice();
    const Neuron::Voice third = device.CreateVoice(sound, false, "third");
    Assert::IsTrue(static_cast<bool>(third), L"a voice that is gone makes room for another");
    AssertNoFailure(messages);
  }

  TEST_METHOD(AVoiceOutlivesItsDevice)
  {
    Messages messages;
    Neuron::Voice voice;
    {
      Neuron::AudioDevice device(DescFor(messages));
      voice = device.CreateVoice(Silence(), true, "loop");
    }
    Assert::IsFalse(static_cast<bool>(voice));
    Assert::IsTrue(voice.IsFinished());
    voice.Start();
    voice.SetVolume(0.5f);
    voice.SetPan(1.0f);
    voice.SetFrequencyRatio(2.0f);
    voice.Seek(10);
    voice.Spatialize({});
    voice.Stop();
    AssertNoFailure(messages);
  }
};

} // namespace NeuronClientTests
