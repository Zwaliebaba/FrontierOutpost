#include "pch.h"
#include "CppUnitTest.h"

#include "LoopbackTransport.h"
#include "Protocol.h"
#include "Trigonometry.h"

#include <cmath>
#include <numbers>
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

} // namespace NeuronCoreTests
