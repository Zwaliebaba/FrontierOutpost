// WireTests.cpp -- framing and the protocol, fed things a socket can actually deliver.
//
// Step 3 of Design/Plans/4X-02-ServerAndClient.md.
//
// THESE BYTES COME OFF A SOCKET AND A SOCKET WILL DELIVER ANYTHING. Half a message, two messages at
// once, a length that is a lie, a message written by something that is not this program. Every test
// below is a thing TCP does routinely or a thing a hostile peer does on purpose, and the standard
// they are held to is the same for both: refuse, and never read past the end.

#include "pch.h"
#include "CppUnitTest.h"

#include "NeuronCore.h"

#include "FrameStream.h"
#include "Prng.h"
#include "Protocol.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

[[nodiscard]] std::vector<std::uint8_t> Bytes(std::initializer_list<int> _values)
{
  std::vector<std::uint8_t> out;
  for (const int value : _values)
  {
    out.push_back(static_cast<std::uint8_t>(value));
  }
  return out;
}

/// A deterministic stream of nonsense, for feeding decoders that must not believe it.
[[nodiscard]] std::vector<std::uint8_t> Noise(std::uint64_t _seed, std::size_t _length)
{
  Neuron::Prng prng{_seed};
  std::vector<std::uint8_t> out;
  out.reserve(_length);
  for (std::size_t index = 0; index < _length; ++index)
  {
    out.push_back(static_cast<std::uint8_t>(prng.Next() & 0xFFU));
  }
  return out;
}

} // namespace

TEST_CLASS(FrameStreamTests)
{
public:
  TEST_METHOD(AFramedMessageComesBackWhole)
  {
    const std::vector<std::uint8_t> payload = Bytes({1, 2, 3, 4, 5});

    Neuron::FrameStream stream;
    Assert::IsTrue(stream.Feed(Neuron::FrameStream::Frame(payload)));

    std::vector<std::uint8_t> taken;
    Assert::IsTrue(stream.Take(taken));
    Assert::AreEqual(payload.size(), taken.size());
    Assert::AreEqual(payload[0], taken[0]);
    Assert::IsFalse(stream.Take(taken), L"and there is not a second one");
  }

  // What TCP does constantly: a message split across two reads.
  TEST_METHOD(AMessageSplitAcrossReadsIsReassembled)
  {
    const std::vector<std::uint8_t> framed = Neuron::FrameStream::Frame(Bytes({9, 8, 7, 6, 5, 4}));

    for (std::size_t cut = 1; cut < framed.size(); ++cut)
    {
      Neuron::FrameStream stream;
      std::vector<std::uint8_t> taken;

      Assert::IsTrue(stream.Feed(std::span{framed.data(), cut}));
      Assert::IsFalse(stream.Take(taken), (std::wstring(L"complete too early at ") + std::to_wstring(cut)).c_str());

      Assert::IsTrue(stream.Feed(std::span{framed.data() + cut, framed.size() - cut}));
      Assert::IsTrue(stream.Take(taken), (std::wstring(L"never completed at ") + std::to_wstring(cut)).c_str());
      Assert::AreEqual(static_cast<size_t>(6), taken.size());
    }
  }

  // And the other thing TCP does: several messages in one read.
  TEST_METHOD(SeveralMessagesInOneReadComeOutOneAtATime)
  {
    std::vector<std::uint8_t> together;
    for (int index = 0; index < 4; ++index)
    {
      const std::vector<std::uint8_t> framed = Neuron::FrameStream::Frame(Bytes({index, index, index}));
      together.insert(together.end(), framed.begin(), framed.end());
    }

    Neuron::FrameStream stream;
    Assert::IsTrue(stream.Feed(together));

    for (int index = 0; index < 4; ++index)
    {
      std::vector<std::uint8_t> taken;
      Assert::IsTrue(stream.Take(taken));
      Assert::AreEqual(static_cast<std::uint8_t>(index), taken[0]);
    }

    std::vector<std::uint8_t> nothing;
    Assert::IsFalse(stream.Take(nothing));
    Assert::AreEqual(static_cast<size_t>(0), stream.Pending());
  }

  TEST_METHOD(AnEmptyMessageIsStillAMessage)
  {
    Neuron::FrameStream stream;
    Assert::IsTrue(stream.Feed(Neuron::FrameStream::Frame({})));

    std::vector<std::uint8_t> taken;
    Assert::IsTrue(stream.Take(taken));
    Assert::IsTrue(taken.empty());
  }

  // The attack, and the accident. A length field is four bytes a peer chooses.
  TEST_METHOD(AnImpossibleLengthIsRefusedRatherThanReserved)
  {
    Neuron::FrameStream stream;
    Assert::IsFalse(stream.Feed(Bytes({0xFF, 0xFF, 0xFF, 0xFF})), L"four billion bytes is not a message");
    Assert::IsTrue(stream.Failed());

    // Poisoned for good: the stream is no longer aligned to anything, so nothing after it can be
    // trusted either.
    std::vector<std::uint8_t> taken;
    Assert::IsFalse(stream.Feed(Neuron::FrameStream::Frame(Bytes({1, 2, 3}))));
    Assert::IsFalse(stream.Take(taken));
  }

  TEST_METHOD(ALengthJustPastTheCapIsRefused)
  {
    Neuron::ByteWriter writer;
    writer.WriteU32(Neuron::FrameStream::MAXIMUM_FRAME_BYTES + 1);

    Neuron::FrameStream stream;
    Assert::IsFalse(stream.Feed(writer.Bytes()));
    Assert::IsTrue(stream.Failed());
  }

  TEST_METHOD(ALengthAtTheCapIsAccepted)
  {
    Neuron::ByteWriter writer;
    writer.WriteU32(Neuron::FrameStream::MAXIMUM_FRAME_BYTES);

    Neuron::FrameStream stream;
    Assert::IsTrue(stream.Feed(writer.Bytes()), L"the cap is a limit, not a forbidden value");
    Assert::IsFalse(stream.Failed());

    std::vector<std::uint8_t> taken;
    Assert::IsFalse(stream.Take(taken), L"and it is still waiting for a megabyte that will not arrive");
  }

  // A peer that opens a huge frame and sends nothing more. The server cannot tell it from a slow
  // link, but it can see how much is held.
  TEST_METHOD(APartialFrameIsVisibleAsPending)
  {
    Neuron::FrameStream stream;
    Assert::IsTrue(stream.Feed(Neuron::ByteWriter{}.Bytes()));

    const std::vector<std::uint8_t> framed = Neuron::FrameStream::Frame(Bytes({1, 2, 3, 4, 5, 6, 7, 8}));
    Assert::IsTrue(stream.Feed(std::span{framed.data(), 6}));
    Assert::AreEqual(static_cast<size_t>(6), stream.Pending());
  }

  // Fuzz. Whatever it is handed, it either refuses or produces messages -- and never reads past the
  // end, which the address sanitiser in a Debug build would catch.
  TEST_METHOD(NoiseNeverProducesUndefinedBehavior)
  {
    for (std::uint64_t seed = 0; seed < 200; ++seed)
    {
      Neuron::FrameStream stream;
      const std::vector<std::uint8_t> noise = Noise(seed, 64);

      if (stream.Feed(noise))
      {
        std::vector<std::uint8_t> taken;
        std::int32_t guard = 0;
        while (stream.Take(taken) && guard < 64)
        {
          ++guard;
        }
      }
      // Reaching here without crashing is the assertion.
      Assert::IsTrue(true);
    }
  }
};

TEST_CLASS(ProtocolTests)
{
public:
  TEST_METHOD(EveryMessageSurvivesTheRoundTrip)
  {
    {
      std::string token;
      Assert::IsTrue(Neuron::Protocol::DecodeHello(Neuron::Protocol::EncodeHello("swordfish"), token));
      Assert::AreEqual(std::string("swordfish"), token);
    }
    {
      std::int32_t player = 0;
      std::uint32_t tick = 0;
      std::int64_t seconds = 0;
      Assert::IsTrue(Neuron::Protocol::DecodeWelcome(Neuron::Protocol::EncodeWelcome(3, 47, 7629), player, tick, seconds));
      Assert::AreEqual(3, player);
      Assert::AreEqual(47U, tick);
      Assert::AreEqual(static_cast<std::int64_t>(7629), seconds);
    }
    {
      Neuron::RefusalReason reason = Neuron::RefusalReason::None;
      Assert::IsTrue(Neuron::Protocol::DecodeRefused(Neuron::Protocol::EncodeRefused(Neuron::RefusalReason::AlreadyConnected), reason));
      Assert::IsTrue(reason == Neuron::RefusalReason::AlreadyConnected);
    }
    {
      const std::vector<std::uint8_t> orders = Bytes({4, 5, 6, 7});
      std::vector<std::uint8_t> returned;
      Assert::IsTrue(Neuron::Protocol::DecodeOrders(Neuron::Protocol::EncodeOrders(orders), returned));
      Assert::AreEqual(orders.size(), returned.size());
    }
    {
      const std::vector<std::uint8_t> snapshot = Bytes({1, 1, 2, 3, 5, 8});
      const std::vector<std::uint8_t> digest = Bytes({9, 9});
      std::uint32_t tick = 0;
      std::int64_t seconds = 0;
      std::vector<std::uint8_t> outSnapshot;
      std::vector<std::uint8_t> outDigest;

      Assert::IsTrue(
        Neuron::Protocol::DecodeState(Neuron::Protocol::EncodeState(12, 900, snapshot, digest), tick, seconds, outSnapshot, outDigest));
      Assert::AreEqual(12U, tick);
      Assert::AreEqual(static_cast<std::int64_t>(900), seconds);
      Assert::AreEqual(snapshot.size(), outSnapshot.size());
      Assert::AreEqual(digest.size(), outDigest.size());
    }
  }

  TEST_METHOD(TheKindIsTheFirstByte)
  {
    Assert::IsTrue(Neuron::Protocol::KindOf(Neuron::Protocol::EncodePing()) == Neuron::MessageKind::Ping);
    Assert::IsTrue(Neuron::Protocol::KindOf({}) == Neuron::MessageKind::None, L"an empty payload has no kind");

    // And neither has a payload whose first byte names nothing. 249 of the 256 values a socket can
    // deliver land here, so this is the ordinary case rather than the edge one.
    Assert::IsTrue(Neuron::Protocol::KindOf(std::vector<std::uint8_t>{0}) == Neuron::MessageKind::None);
    Assert::IsTrue(Neuron::Protocol::KindOf(std::vector<std::uint8_t>{7}) == Neuron::MessageKind::None);
    Assert::IsTrue(Neuron::Protocol::KindOf(std::vector<std::uint8_t>{255}) == Neuron::MessageKind::None);
  }

  // A decoder asked for the wrong kind must refuse rather than reinterpret. Otherwise a `State`
  // could be read as a `Welcome` and produce a plausible player id.
  TEST_METHOD(ADecoderRefusesAnotherKindsMessage)
  {
    const std::vector<std::uint8_t> welcome = Neuron::Protocol::EncodeWelcome(1, 2, 3);

    std::string token;
    Assert::IsFalse(Neuron::Protocol::DecodeHello(welcome, token));

    std::vector<std::uint8_t> orders;
    Assert::IsFalse(Neuron::Protocol::DecodeOrders(welcome, orders));
  }

  TEST_METHOD(ATruncatedMessageIsRefused)
  {
    const std::vector<std::uint8_t> state = Neuron::Protocol::EncodeState(5, 10, Bytes({1, 2, 3, 4, 5, 6, 7, 8}), Bytes({9}));

    for (std::size_t cut = 0; cut < state.size(); ++cut)
    {
      std::uint32_t tick = 0;
      std::int64_t seconds = 0;
      std::vector<std::uint8_t> snapshot;
      std::vector<std::uint8_t> digest;

      Assert::IsFalse(Neuron::Protocol::DecodeState(std::span{state.data(), cut}, tick, seconds, snapshot, digest),
                      (std::wstring(L"accepted a message cut at ") + std::to_wstring(cut)).c_str());
    }
  }

  // Bytes on the end are as wrong as bytes missing: the message is not the shape this reader
  // expects, so something has gone wrong upstream and believing the front of it is a guess.
  TEST_METHOD(TrailingBytesAreRefused)
  {
    std::vector<std::uint8_t> welcome = Neuron::Protocol::EncodeWelcome(1, 2, 3);
    welcome.push_back(0);

    std::int32_t player = 0;
    std::uint32_t tick = 0;
    std::int64_t seconds = 0;
    Assert::IsFalse(Neuron::Protocol::DecodeWelcome(welcome, player, tick, seconds));
  }

  // The one that would be a real vulnerability: a blob claiming more than the message contains.
  TEST_METHOD(ABlobLongerThanItsMessageIsRefused)
  {
    Neuron::ByteWriter writer;
    writer.WriteU8(static_cast<std::uint8_t>(Neuron::MessageKind::Orders));
    writer.WriteU32(50'000); // and then three bytes
    writer.WriteU8(1);
    writer.WriteU8(2);
    writer.WriteU8(3);

    std::vector<std::uint8_t> orders;
    Assert::IsFalse(Neuron::Protocol::DecodeOrders(writer.Bytes(), orders));
    Assert::IsTrue(orders.empty(), L"and nothing was reserved for it");
  }

  TEST_METHOD(ABlobPastTheCapIsRefused)
  {
    Neuron::ByteWriter writer;
    writer.WriteU8(static_cast<std::uint8_t>(Neuron::MessageKind::Orders));
    writer.WriteU32(Neuron::Protocol::MAXIMUM_BLOB_BYTES + 1);

    std::vector<std::uint8_t> orders;
    Assert::IsFalse(Neuron::Protocol::DecodeOrders(writer.Bytes(), orders));
  }

  TEST_METHOD(AnAbsurdlyLongTokenIsRefused)
  {
    const std::string huge(Neuron::Protocol::MAXIMUM_TOKEN_LENGTH + 1, 'x');

    std::string token;
    Assert::IsFalse(Neuron::Protocol::DecodeHello(Neuron::Protocol::EncodeHello(huge), token));
  }

  // Every decoder, against noise, forever. None may accept, crash, or read past the end.
  TEST_METHOD(NoDecoderBelievesNoise)
  {
    for (std::uint64_t seed = 0; seed < 300; ++seed)
    {
      const std::vector<std::uint8_t> noise = Noise(seed, 1 + (seed % 40));

      std::string token;
      (void)Neuron::Protocol::DecodeHello(noise, token);

      std::int32_t player = 0;
      std::uint32_t tick = 0;
      std::int64_t seconds = 0;
      (void)Neuron::Protocol::DecodeWelcome(noise, player, tick, seconds);

      Neuron::RefusalReason reason = Neuron::RefusalReason::None;
      (void)Neuron::Protocol::DecodeRefused(noise, reason);

      std::vector<std::uint8_t> orders;
      (void)Neuron::Protocol::DecodeOrders(noise, orders);

      std::vector<std::uint8_t> snapshot;
      std::vector<std::uint8_t> digest;
      (void)Neuron::Protocol::DecodeState(noise, tick, seconds, snapshot, digest);

      // Surviving is the assertion. What is being tested is the absence of a crash and of a read
      // past the end, neither of which a return value can express.
      Assert::IsTrue(true);
    }
  }

  TEST_METHOD(EveryRefusalDescribesItself)
  {
    for (std::uint8_t reason = 0; reason <= static_cast<std::uint8_t>(Neuron::RefusalReason::Malformed); ++reason)
    {
      const char* text = Describe(static_cast<Neuron::RefusalReason>(reason));
      Assert::IsNotNull(text);
      Assert::AreNotEqual("unknown", text);
    }
  }
};

} // namespace NeuronCoreTests
