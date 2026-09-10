#include "pch.h"
#include "CppUnitTest.h"

#include "LoopbackTransport.h"
#include "Protocol.h"
#include "Trigonometry.h"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

// The wire format is the one thing in this tree that two pieces of code have to agree about
// without being compiled together. Round-tripping is the minimum; the sizes and the byte order
// are the part that would otherwise only be discovered by a second implementation.
TEST_CLASS(ProtocolTests)
{
public:
  TEST_METHOD(TheRecordSizesAreFixed)
  {
    Assert::AreEqual(static_cast<size_t>(16), Neuron::MOVE_TO_ORDER_BYTES);
    Assert::AreEqual(static_cast<size_t>(32), Neuron::SHIP_STATE_BYTES);
  }

  TEST_METHOD(AnOrderSurvivesTheRoundTrip)
  {
    const Neuron::MoveToOrder original = {.targetXMillimetres = -1234567890123LL, .targetZMillimetres = 987654321LL};

    std::array<std::byte, Neuron::MOVE_TO_ORDER_BYTES> bytes = {};
    Neuron::Serialize(original, bytes);
    const Neuron::MoveToOrder restored = Neuron::DeserializeMoveToOrder(bytes);

    Assert::AreEqual(original.targetXMillimetres, restored.targetXMillimetres);
    Assert::AreEqual(original.targetZMillimetres, restored.targetZMillimetres);
  }

  TEST_METHOD(AStateSurvivesTheRoundTrip)
  {
    const Neuron::ShipState original = {
      .tick = 0xFEDCBA9876543210ULL,
      .positionXMillimetres = std::numeric_limits<std::int64_t>::min() + 1,
      .positionZMillimetres = std::numeric_limits<std::int64_t>::max(),
      .speedMillimetresPerTick = -32768,
      .headingTurns16 = 40000,
      .reserved = 0,
    };

    std::array<std::byte, Neuron::SHIP_STATE_BYTES> bytes = {};
    Neuron::Serialize(original, bytes);
    const Neuron::ShipState restored = Neuron::DeserializeShipState(bytes);

    Assert::AreEqual(original.tick, restored.tick);
    Assert::AreEqual(original.positionXMillimetres, restored.positionXMillimetres);
    Assert::AreEqual(original.positionZMillimetres, restored.positionZMillimetres);
    Assert::AreEqual(original.speedMillimetresPerTick, restored.speedMillimetresPerTick);
    Assert::AreEqual(original.headingTurns16, restored.headingTurns16);
  }

  // Little-endian by construction, not by whatever this machine happens to be. Pinning the actual
  // bytes is the only way to notice the day somebody replaces the shifts with a memcpy.
  TEST_METHOD(TheBytesAreLittleEndian)
  {
    const Neuron::MoveToOrder order = {.targetXMillimetres = 0x0102030405060708LL, .targetZMillimetres = 0};

    std::array<std::byte, Neuron::MOVE_TO_ORDER_BYTES> bytes = {};
    Neuron::Serialize(order, bytes);

    Assert::AreEqual(static_cast<std::uint8_t>(0x08), static_cast<std::uint8_t>(bytes[0]), L"least significant byte first");
    Assert::AreEqual(static_cast<std::uint8_t>(0x01), static_cast<std::uint8_t>(bytes[7]));
  }
};

// Integer trigonometry (R16). The errors quoted in Trigonometry.h are measured here, so the
// header's claims and the tests cannot drift apart.
TEST_CLASS(TrigonometryTests)
{
public:
  TEST_METHOD(SineAndCosineAreCloseToTheRealThing)
  {
    std::int32_t worstError = 0;
    for (std::int32_t angle = 0; angle < Neuron::TURNS16_PER_TURN; angle += 7)
    {
      const Neuron::SineCosine got = Neuron::SineCosineTurns16(static_cast<Neuron::Turns16>(angle));
      const double radians = angle * 2.0 * std::numbers::pi / Neuron::TURNS16_PER_TURN;

      const auto sineError = static_cast<std::int32_t>(std::abs(got.sine - std::sin(radians) * Neuron::TRIG_ONE));
      const auto cosineError = static_cast<std::int32_t>(std::abs(got.cosine - std::cos(radians) * Neuron::TRIG_ONE));
      worstError = std::max({worstError, sineError, cosineError});
    }

    // Trigonometry.h says 19 of 65536. If this ever exceeds it, the header is wrong too.
    Assert::IsTrue(worstError <= 19, (std::wstring(L"worst sine/cosine error was ") + std::to_wstring(worstError)).c_str());
  }

  TEST_METHOD(TheCardinalDirectionsAreExact)
  {
    // Heading 0 is +X, and a quarter turn later is +Z. Everything in the game's geometry rests on
    // this, so it is asserted rather than inferred from the CORDIC being close.
    const Neuron::SineCosine east = Neuron::SineCosineTurns16(0);
    Assert::AreEqual(Neuron::TRIG_ONE, east.cosine);
    Assert::AreEqual(0, east.sine);

    const Neuron::SineCosine south = Neuron::SineCosineTurns16(static_cast<Neuron::Turns16>(Neuron::TURNS16_PER_QUARTER_TURN));
    Assert::AreEqual(0, south.cosine);
    Assert::AreEqual(Neuron::TRIG_ONE, south.sine);
  }

  TEST_METHOD(Atan2RecoversTheHeadingItWasGiven)
  {
    std::int32_t worstError = 0;
    for (std::int32_t angle = 0; angle < Neuron::TURNS16_PER_TURN; angle += 13)
    {
      const Neuron::SineCosine direction = Neuron::SineCosineTurns16(static_cast<Neuron::Turns16>(angle));
      const Neuron::Turns16 recovered = Neuron::Atan2Turns16(direction.cosine, direction.sine);

      const std::int32_t error = std::abs(Neuron::ShortestTurnTurns16(static_cast<Neuron::Turns16>(angle), recovered));
      worstError = std::max(worstError, error);
    }

    Assert::IsTrue(worstError <= 4, (std::wstring(L"worst round trip was ") + std::to_wstring(worstError)).c_str());
  }

  TEST_METHOD(Atan2HandlesTheAxesAndTheOrigin)
  {
    Assert::AreEqual(static_cast<Neuron::Turns16>(0), Neuron::Atan2Turns16(1000, 0), L"+X is heading zero");
    Assert::AreEqual(static_cast<Neuron::Turns16>(16384), Neuron::Atan2Turns16(0, 1000), L"+Z is a quarter turn");
    Assert::AreEqual(static_cast<Neuron::Turns16>(32768), Neuron::Atan2Turns16(-1000, 0), L"-X is a half turn");
    Assert::AreEqual(static_cast<Neuron::Turns16>(49152), Neuron::Atan2Turns16(0, -1000), L"-Z is three quarters");
    Assert::AreEqual(static_cast<Neuron::Turns16>(0), Neuron::Atan2Turns16(0, 0), L"a zero vector has no direction");
  }

  // The distances the simulation deals in are millimetres over interplanetary ranges, so atan2 has
  // to work at both ends of the range and not just at the size of a test fixture. The axes stay
  // exact at every magnitude; the diagonal is only close, because 45 degrees is exactly where
  // CORDIC's first step lands and there is nothing left to correct it with.
  TEST_METHOD(Atan2WorksAtEveryMagnitude)
  {
    for (const std::int64_t magnitude : {std::int64_t{1}, std::int64_t{7}, std::int64_t{1000}, std::int64_t{1} << 50})
    {
      const std::wstring what = std::wstring(L"at magnitude ") + std::to_wstring(magnitude);
      Assert::AreEqual(static_cast<Neuron::Turns16>(0), Neuron::Atan2Turns16(magnitude, 0), what.c_str());
      Assert::AreEqual(static_cast<Neuron::Turns16>(16384), Neuron::Atan2Turns16(0, magnitude), what.c_str());
      Assert::AreEqual(static_cast<Neuron::Turns16>(32768), Neuron::Atan2Turns16(-magnitude, 0), what.c_str());
      Assert::AreEqual(static_cast<Neuron::Turns16>(49152), Neuron::Atan2Turns16(0, -magnitude), what.c_str());

      const std::int32_t diagonalError = std::abs(Neuron::ShortestTurnTurns16(8192, Neuron::Atan2Turns16(magnitude, magnitude)));
      Assert::IsTrue(diagonalError <= 9, what.c_str());
    }
  }

  TEST_METHOD(ShortestTurnGoesTheShortWay)
  {
    Assert::AreEqual(1000, Neuron::ShortestTurnTurns16(0, 1000));
    Assert::AreEqual(-1000, Neuron::ShortestTurnTurns16(1000, 0));
    // Across the wrap: 65000 to 500 is 1036 forwards, not 64500 backwards.
    Assert::AreEqual(1036, Neuron::ShortestTurnTurns16(65000, 500));
    Assert::AreEqual(-1036, Neuron::ShortestTurnTurns16(500, 65000));
  }

  TEST_METHOD(IntegerSquareRootIsExact)
  {
    Assert::AreEqual(0ULL, Neuron::IntegerSquareRoot(0));
    Assert::AreEqual(1ULL, Neuron::IntegerSquareRoot(1));
    Assert::AreEqual(1ULL, Neuron::IntegerSquareRoot(3), L"floor, not round");
    Assert::AreEqual(2ULL, Neuron::IntegerSquareRoot(4));
    Assert::AreEqual(1000ULL, Neuron::IntegerSquareRoot(1000000));
    Assert::AreEqual(4294967295ULL, Neuron::IntegerSquareRoot(std::numeric_limits<std::uint64_t>::max()));

    for (std::uint64_t root = 0; root < 3000; ++root)
    {
      Assert::AreEqual(root, Neuron::IntegerSquareRoot(root * root));
      if (root > 0)
      {
        Assert::AreEqual(root - 1, Neuron::IntegerSquareRoot(root * root - 1), L"one below a square is the root below");
      }
    }
  }

  TEST_METHOD(SaturatingAddStopsAtTheEnds)
  {
    constexpr std::int64_t BIGGEST = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t SMALLEST = std::numeric_limits<std::int64_t>::min();

    Assert::AreEqual(BIGGEST, Neuron::SaturatingAdd(BIGGEST, 1));
    Assert::AreEqual(BIGGEST, Neuron::SaturatingAdd(BIGGEST - 5, 1000));
    Assert::AreEqual(SMALLEST, Neuron::SaturatingAdd(SMALLEST, -1));
    Assert::AreEqual(7LL, Neuron::SaturatingAdd(3, 4), L"and adds normally in the middle");
  }
};

// The transport with both ends in one process (MVP-01 step 5). The point of these is the
// concurrency: a queue that works when one thread uses it proves nothing about the case it exists
// for.
TEST_CLASS(LoopbackTransportTests)
{
public:
  TEST_METHOD(AnEmptyTransportHasNothingToReceive)
  {
    Neuron::LoopbackTransport transport;

    Neuron::MoveToOrder order = {};
    Neuron::ShipState state = {};
    Assert::IsFalse(transport.ReceiveOrder(order));
    Assert::IsFalse(transport.ReceiveState(state));
  }

  TEST_METHOD(OrdersArriveAtTheServerEndInOrder)
  {
    Neuron::LoopbackTransport transport;
    for (std::int64_t index = 0; index < 10; ++index)
    {
      Assert::IsTrue(transport.SendOrder({.targetXMillimetres = index, .targetZMillimetres = -index}));
    }

    for (std::int64_t index = 0; index < 10; ++index)
    {
      Neuron::MoveToOrder order = {};
      Assert::IsTrue(transport.ReceiveOrder(order));
      Assert::AreEqual(index, order.targetXMillimetres);
      Assert::AreEqual(-index, order.targetZMillimetres);
    }

    Neuron::MoveToOrder drained = {};
    Assert::IsFalse(transport.ReceiveOrder(drained));
  }

  TEST_METHOD(StatesArriveAtTheClientEnd)
  {
    Neuron::LoopbackTransport transport;
    Assert::IsTrue(transport.SendState({.tick = 42}));

    Neuron::ShipState state = {};
    Assert::IsTrue(transport.ReceiveState(state));
    Assert::AreEqual(42ULL, state.tick);
  }

  TEST_METHOD(TheTwoDirectionsDoNotShareAQueue)
  {
    Neuron::LoopbackTransport transport;
    transport.SendOrder({.targetXMillimetres = 1, .targetZMillimetres = 2});

    Neuron::ShipState state = {};
    Assert::IsFalse(transport.ReceiveState(state), L"an order must not come back as a state");
  }

  // ADR-006: orders drop the newest, states drop the oldest. Both policies are deliberate and
  // both are the opposite of the other one being right.
  TEST_METHOD(AFullOrderQueueDropsTheNewest)
  {
    Neuron::LoopbackTransport transport;
    for (std::int64_t index = 0; index < static_cast<std::int64_t>(Neuron::LoopbackTransport::ORDER_CAPACITY); ++index)
    {
      Assert::IsTrue(transport.SendOrder({.targetXMillimetres = index, .targetZMillimetres = 0}));
    }

    Assert::IsFalse(transport.SendOrder({.targetXMillimetres = 9999, .targetZMillimetres = 0}), L"the queue is full");
    Assert::AreEqual(1ULL, transport.DroppedOrderCount());

    Neuron::MoveToOrder first = {};
    Assert::IsTrue(transport.ReceiveOrder(first));
    Assert::AreEqual(0LL, first.targetXMillimetres, L"the oldest order is still there");
  }

  TEST_METHOD(AFullStateQueueDropsTheOldest)
  {
    Neuron::LoopbackTransport transport;
    for (std::uint64_t tick = 0; tick < Neuron::LoopbackTransport::STATE_CAPACITY; ++tick)
    {
      Assert::IsTrue(transport.SendState({.tick = tick}));
    }

    transport.SendState({.tick = 9999});
    Assert::AreEqual(1ULL, transport.DroppedStateCount());

    Neuron::ShipState oldest = {};
    Assert::IsTrue(transport.ReceiveState(oldest));
    Assert::AreEqual(1ULL, oldest.tick, L"tick 0 was dropped to make room for the newest");
  }

  // Two threads, which is the case this class exists for. Every order sent must arrive exactly
  // once and in order; anything else is a race in the queue.
  TEST_METHOD(SurvivesAProducerAndAConsumerOnDifferentThreads)
  {
    constexpr std::int64_t MESSAGE_COUNT = 20000;
    Neuron::LoopbackTransport transport;

    std::vector<std::int64_t> received;
    received.reserve(MESSAGE_COUNT);

    std::thread consumer(
      [&transport, &received]
      {
        while (static_cast<std::int64_t>(received.size()) < MESSAGE_COUNT)
        {
          Neuron::MoveToOrder order = {};
          if (transport.ReceiveOrder(order))
          {
            received.push_back(order.targetXMillimetres);
          }
          else
          {
            std::this_thread::yield();
          }
        }
      });

    for (std::int64_t index = 0; index < MESSAGE_COUNT; ++index)
    {
      // The capacity is far below MESSAGE_COUNT, so this spins when the consumer falls behind --
      // which is what exercises the wrap-around rather than just the happy path.
      while (!transport.SendOrder({.targetXMillimetres = index, .targetZMillimetres = 0}))
      {
        std::this_thread::yield();
      }
    }

    consumer.join();

    // Every message arrives exactly once and in order. The transport's dropped count is NOT
    // asserted to be zero: the producer above spins on a refusal, so a full queue costs a retry
    // rather than a message, and the counter counts times the queue was full.
    Assert::AreEqual(static_cast<size_t>(MESSAGE_COUNT), received.size());
    for (std::int64_t index = 0; index < MESSAGE_COUNT; ++index)
    {
      if (received[static_cast<std::size_t>(index)] != index)
      {
        Assert::Fail((std::wstring(L"out of order at ") + std::to_wstring(index)).c_str());
      }
    }
  }
};

namespace
{

/// A snapshot with something in every list, so a round trip exercises each one rather than the
/// happy path of the first.
Neuron::VisibleSnapshot SampleSnapshot()
{
  Neuron::VisibleSnapshot snapshot;
  snapshot.tick = 31;
  snapshot.endTick = 84;
  snapshot.sealedOpensTick = 42;
  snapshot.seat = 3;

  snapshot.systems.push_back(Neuron::SystemView{.systemId = 0,
                                                .xUnits = -120,
                                                .yUnits = 77,
                                                .kind = 1,
                                                .visibility = Neuron::Visibility::Observed,
                                                .owner = 3,
                                                .reserved = 0,
                                                .yieldPerTick = 6,
                                                .garrisonStrength = 10,
                                                .observedTick = 31});
  snapshot.systems.push_back(Neuron::SystemView{.systemId = 1,
                                                .xUnits = 5,
                                                .yUnits = -9,
                                                .kind = 3,
                                                .visibility = Neuron::Visibility::Unknown,
                                                .owner = 0xFF,
                                                .reserved = 0,
                                                .yieldPerTick = 0,
                                                .garrisonStrength = 0,
                                                .observedTick = 0});
  snapshot.lanes.push_back(Neuron::LaneView{.laneId = 0, .endA = 0, .endB = 1, .costTicks = 3});
  snapshot.transits.push_back(Neuron::TransitView{
    .fleetId = 7, .laneId = 0, .towardSystemId = 1, .owner = 2, .reserved = 0, .strength = 40, .departedTick = 29, .arrivesTick = 32});
  snapshot.seats.push_back(Neuron::SeatView{.seatId = 3, .reserved0 = 0, .capitalSystemId = 0, .score = 0, .capitalGuardEndsTick = 12});
  return snapshot;
}

} // namespace

// The frame: a length, a type and a version in front of every payload (ADR-006). Its job is to
// turn a malformed or unexpected message into a refusal rather than into a record read out of
// whatever followed it in memory, so most of these tests are about the refusals.
TEST_CLASS(FrameTests)
{
public:
  TEST_METHOD(AFrameSurvivesTheRoundTrip)
  {
    const Neuron::Frame original{
      .type = Neuron::MessageType::VisibleSnapshot, .version = Neuron::PROTOCOL_VERSION, .payload = {std::byte{1}, std::byte{2}}};

    std::vector<std::byte> bytes;
    Neuron::EncodeFrame(original, bytes);
    Assert::AreEqual(Neuron::FRAME_HEADER_BYTES + 2, bytes.size());

    Neuron::Frame restored = {};
    Assert::IsTrue(Neuron::DecodeFrame(bytes, restored));
    Assert::AreEqual(static_cast<int>(original.type), static_cast<int>(restored.type));
    Assert::AreEqual(original.version, restored.version);
    Assert::AreEqual(original.payload.size(), restored.payload.size());
    Assert::IsTrue(original.payload == restored.payload);
  }

  TEST_METHOD(AnEmptyPayloadIsStillAFrame)
  {
    const Neuron::Frame original{.type = Neuron::MessageType::None, .version = Neuron::PROTOCOL_VERSION, .payload = {}};
    std::vector<std::byte> bytes;
    Neuron::EncodeFrame(original, bytes);
    Assert::AreEqual(Neuron::FRAME_HEADER_BYTES, bytes.size());

    Neuron::Frame restored = {};
    Assert::IsTrue(Neuron::DecodeFrame(bytes, restored));
    Assert::IsTrue(restored.payload.empty());
  }

  TEST_METHOD(ABufferShorterThanTheHeaderIsRefused)
  {
    const std::vector<std::byte> tooShort(Neuron::FRAME_HEADER_BYTES - 1, std::byte{0});
    Neuron::Frame restored = {};
    Assert::IsFalse(Neuron::DecodeFrame(tooShort, restored));
  }

  TEST_METHOD(ALengthThatDisagreesWithTheBufferIsRefused)
  {
    const Neuron::Frame original{
      .type = Neuron::MessageType::VisibleSnapshot, .version = Neuron::PROTOCOL_VERSION, .payload = {std::byte{9}}};
    std::vector<std::byte> bytes;
    Neuron::EncodeFrame(original, bytes);

    // Claim two payload bytes and supply one. This is the truncation a socket produces, and it
    // must not be read as a one-byte payload.
    bytes[0] = std::byte{2};
    Neuron::Frame restored = {};
    Assert::IsFalse(Neuron::DecodeFrame(bytes, restored));
  }

  TEST_METHOD(AVersionThisBuildDoesNotKnowIsRefused)
  {
    const Neuron::Frame original{
      .type = Neuron::MessageType::VisibleSnapshot, .version = Neuron::PROTOCOL_VERSION, .payload = {std::byte{9}}};
    std::vector<std::byte> bytes;
    Neuron::EncodeFrame(original, bytes);

    bytes[6] = std::byte{0xFE};
    bytes[7] = std::byte{0xFF};
    Neuron::Frame restored = {};
    Assert::IsFalse(Neuron::DecodeFrame(bytes, restored));
  }
};

// The snapshot: variable length, so unlike the two MVP-01 records it has no compile-time size and
// every read has to be bounds-checked (ADR-005).
TEST_CLASS(VisibleSnapshotTests)
{
public:
  TEST_METHOD(ASnapshotSurvivesTheRoundTrip)
  {
    const Neuron::VisibleSnapshot original = SampleSnapshot();

    std::vector<std::byte> bytes;
    Neuron::Serialize(original, bytes);

    Neuron::VisibleSnapshot restored;
    Assert::IsTrue(Neuron::DeserializeVisibleSnapshot(bytes, restored));

    Assert::AreEqual(original.tick, restored.tick);
    Assert::AreEqual(original.endTick, restored.endTick);
    Assert::AreEqual(original.sealedOpensTick, restored.sealedOpensTick);
    Assert::AreEqual(original.seat, restored.seat);
    Assert::AreEqual(original.systems.size(), restored.systems.size());
    Assert::AreEqual(original.lanes.size(), restored.lanes.size());
    Assert::AreEqual(original.transits.size(), restored.transits.size());
    Assert::AreEqual(original.seats.size(), restored.seats.size());

    // A negative coordinate is the field most likely to be wrong, because it is the only signed
    // one that goes through the unsigned shift path.
    Assert::AreEqual(-120, restored.systems[0].xUnits);
    Assert::AreEqual(77, restored.systems[0].yUnits);
    Assert::AreEqual(static_cast<int>(Neuron::Visibility::Observed), static_cast<int>(restored.systems[0].visibility));
    Assert::AreEqual(static_cast<int>(Neuron::Visibility::Unknown), static_cast<int>(restored.systems[1].visibility));
    Assert::AreEqual(3, restored.lanes[0].costTicks);
    Assert::AreEqual(40, restored.transits[0].strength);
    Assert::AreEqual(29ULL, restored.transits[0].departedTick);
    Assert::AreEqual(12ULL, restored.seats[0].capitalGuardEndsTick);
  }

  TEST_METHOD(AnEmptySnapshotSurvivesTheRoundTrip)
  {
    Neuron::VisibleSnapshot original;
    original.tick = 1;
    original.seat = 0;

    std::vector<std::byte> bytes;
    Neuron::Serialize(original, bytes);

    Neuron::VisibleSnapshot restored = SampleSnapshot();
    Assert::IsTrue(Neuron::DeserializeVisibleSnapshot(bytes, restored));
    Assert::IsTrue(restored.systems.empty());
    Assert::IsTrue(restored.lanes.empty());
  }

  TEST_METHOD(ATruncatedSnapshotIsRefused)
  {
    std::vector<std::byte> bytes;
    Neuron::Serialize(SampleSnapshot(), bytes);

    // Every prefix of a valid record must be refused, not just the obvious ones.
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
      Neuron::VisibleSnapshot restored;
      const std::span<const std::byte> prefix{bytes.data(), length};
      Assert::IsFalse(Neuron::DeserializeVisibleSnapshot(prefix, restored), L"a prefix of a snapshot deserialized as if it were whole");
    }
  }

  TEST_METHOD(TrailingBytesAreRefused)
  {
    std::vector<std::byte> bytes;
    Neuron::Serialize(SampleSnapshot(), bytes);
    bytes.push_back(std::byte{0});

    Neuron::VisibleSnapshot restored;
    Assert::IsFalse(Neuron::DeserializeVisibleSnapshot(bytes, restored),
                    L"a record that leaves a tail is one whose writer and reader disagree");
  }

  TEST_METHOD(AnAbsurdCountIsRefusedRatherThanAllocated)
  {
    std::vector<std::byte> bytes;
    Neuron::Serialize(SampleSnapshot(), bytes);

    // The system count sits after tick, endTick, sealedOpensTick and seat: 8 + 8 + 8 + 1.
    constexpr std::size_t SYSTEM_COUNT_OFFSET = 25;
    bytes[SYSTEM_COUNT_OFFSET] = std::byte{0xFF};
    bytes[SYSTEM_COUNT_OFFSET + 1] = std::byte{0xFF};

    Neuron::VisibleSnapshot restored;
    Assert::IsFalse(Neuron::DeserializeVisibleSnapshot(bytes, restored),
                    L"a count larger than the buffer could hold must fail before it reserves");
  }

  TEST_METHOD(AFailedDeserializeLeavesTheTargetAlone)
  {
    // There is no partial success: the target is only assigned once the whole record has read.
    const Neuron::VisibleSnapshot before = SampleSnapshot();
    Neuron::VisibleSnapshot target = before;

    const std::vector<std::byte> rubbish(10, std::byte{0xAB});
    Assert::IsFalse(Neuron::DeserializeVisibleSnapshot(rubbish, target));
    Assert::AreEqual(before.tick, target.tick);
    Assert::AreEqual(before.systems.size(), target.systems.size());
  }
};

} // namespace NeuronCoreTests
