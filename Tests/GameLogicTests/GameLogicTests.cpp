#include "pch.h"
#include "CppUnitTest.h"

#include "Trigonometry.h"
#include "World.h"

#include <cmath>
#include <numbers>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// Ticks a ship until it has no order left, or until the limit. Returns the ticks it took.
///
/// The limit is a guard rather than a parameter: a ship that has not arrived after this many
/// ticks is not slow, it is orbiting, and a test that hangs is worse than one that fails.
std::uint32_t TickUntilStopped(Frontier::Ship& _ship, std::uint32_t _limitTicks = 4000)
{
  std::uint32_t ticks = 0;
  while (_ship.HasOrder() && ticks < _limitTicks)
  {
    _ship.Tick();
    ++ticks;
  }
  return ticks;
}

/// The stopping distance, by simulating the deceleration rather than by the closed form. This is
/// the definition; Ship::StoppingDistanceMillimetres is the optimization, and the test below is
/// what says they agree.
std::int64_t StoppingDistanceByLoop(std::int32_t _speed)
{
  std::int64_t distance = 0;
  std::int32_t speed = _speed;
  while (speed > 0)
  {
    speed = std::max(0, speed - Frontier::Ship::ACCELERATION_MILLIMETRES_PER_TICK_SQUARED);
    distance += speed;
  }
  return distance;
}

} // namespace

// The kinematics. This is the suite MVP-01 step 5 says will matter most for the rest of the
// game's life, and it runs with no D3D12, no window and no threads -- a Ship is arithmetic.
TEST_CLASS(ShipKinematicsTests)
{
public:
  TEST_METHOD(StartsStillAtTheOriginWithNoOrder)
  {
    const Frontier::Ship ship;
    Assert::AreEqual(0LL, ship.PositionXMillimetres());
    Assert::AreEqual(0LL, ship.PositionZMillimetres());
    Assert::AreEqual(0, ship.SpeedMillimetresPerTick());
    Assert::IsFalse(ship.HasOrder());
  }

  TEST_METHOD(DoesNothingWithoutAnOrder)
  {
    Frontier::Ship ship;
    for (int tick = 0; tick < 100; ++tick)
    {
      ship.Tick();
    }

    Assert::AreEqual(0LL, ship.PositionXMillimetres());
    Assert::AreEqual(0LL, ship.PositionZMillimetres());
  }

  // The closed form against the loop that defines it, at every speed the ship can be at. If these
  // ever disagree the ship stops in the wrong place, and the closed form is the sort of algebra
  // that is wrong by one term without looking wrong.
  TEST_METHOD(TheStoppingDistanceFormulaMatchesTheSimulatedDeceleration)
  {
    for (std::int32_t speed = 0; speed <= Frontier::Ship::MAX_SPEED_MILLIMETRES_PER_TICK; ++speed)
    {
      Assert::AreEqual(StoppingDistanceByLoop(speed), Frontier::Ship::StoppingDistanceMillimetres(speed),
                       (std::wstring(L"stopping distance from speed ") + std::to_wstring(speed)).c_str());
    }
  }

  TEST_METHOD(TurnsTowardsTheTargetNoFasterThanTheTurnRate)
  {
    Frontier::Ship ship;
    // Directly astern: the largest turn there is.
    ship.OrderMoveTo(-100000, 0);

    Neuron::Turns16 previous = ship.HeadingTurns16();
    for (int tick = 0; tick < 40; ++tick)
    {
      ship.Tick();
      const std::int32_t turned = Neuron::ShortestTurnTurns16(previous, ship.HeadingTurns16());
      Assert::IsTrue(std::abs(turned) <= Frontier::Ship::TURN_RATE_TURNS16_PER_TICK,
                     L"the ship turned further in one tick than the turn rate allows");
      previous = ship.HeadingTurns16();
    }
  }

  TEST_METHOD(TurnsTheShortWayRound)
  {
    // A target just clockwise of dead ahead. The ship starts at heading 0 and must turn a little
    // positive, not most of the way round the other way.
    Frontier::Ship ship;
    ship.OrderMoveTo(100000, 20000);
    ship.Tick();

    const std::int32_t turned = Neuron::ShortestTurnTurns16(0, ship.HeadingTurns16());
    Assert::IsTrue(turned > 0, L"a target to starboard is a turn towards +Z");
    Assert::IsTrue(turned <= Frontier::Ship::TURN_RATE_TURNS16_PER_TICK);
  }

  TEST_METHOD(ArrivesExactlyOnTheTargetAndStopped)
  {
    constexpr std::int64_t TARGET_X = 60000; // 60 m dead ahead
    Frontier::Ship ship;
    ship.OrderMoveTo(TARGET_X, 0);

    const std::uint32_t ticks = TickUntilStopped(ship);

    Assert::IsFalse(ship.HasOrder(), L"the ship should have finished its order");
    Assert::AreEqual(TARGET_X, ship.PositionXMillimetres(), L"arrival is exact, not nearby");
    Assert::AreEqual(0LL, ship.PositionZMillimetres());
    Assert::AreEqual(0, ship.SpeedMillimetresPerTick(), L"and stopped");
    Assert::IsTrue(ticks > 0 && ticks < 4000);
  }

  TEST_METHOD(ArrivesAtATargetInEveryDirection)
  {
    // Sixteen directions, including the one directly astern that needs a half turn first.
    for (std::int32_t sixteenth = 0; sixteenth < 16; ++sixteenth)
    {
      const auto heading = static_cast<Neuron::Turns16>(sixteenth * 4096);
      const Neuron::SineCosine direction = Neuron::SineCosineTurns16(heading);
      const std::int64_t targetX = 80000LL * direction.cosine / Neuron::TRIG_ONE;
      const std::int64_t targetZ = 80000LL * direction.sine / Neuron::TRIG_ONE;

      Frontier::Ship ship;
      ship.OrderMoveTo(targetX, targetZ);
      const std::uint32_t ticks = TickUntilStopped(ship);

      Assert::IsFalse(ship.HasOrder(), (std::wstring(L"never arrived, heading ") + std::to_wstring(heading)).c_str());
      Assert::AreEqual(targetX, ship.PositionXMillimetres(), (std::wstring(L"x, heading ") + std::to_wstring(heading)).c_str());
      Assert::AreEqual(targetZ, ship.PositionZMillimetres(), (std::wstring(L"z, heading ") + std::to_wstring(heading)).c_str());
      Assert::AreEqual(0, ship.SpeedMillimetresPerTick());
      Assert::IsTrue(ticks < 4000);
    }
  }

  // The failure the arrival test cannot see: a ship that reaches the target, overshoots, turns
  // round, comes back and only then satisfies "stopped on the target". Distance to the target
  // must never increase once the ship is pointed at it and moving.
  TEST_METHOD(DoesNotOvershootTheTarget)
  {
    constexpr std::int64_t TARGET_X = 100000;
    Frontier::Ship ship;
    ship.OrderMoveTo(TARGET_X, 0);

    std::int64_t furthestX = 0;
    while (ship.HasOrder())
    {
      ship.Tick();
      furthestX = std::max(furthestX, ship.PositionXMillimetres());
    }

    Assert::AreEqual(TARGET_X, furthestX, L"the ship went past the target and came back");
  }

  TEST_METHOD(NeverExceedsTheMaximumSpeed)
  {
    Frontier::Ship ship;
    ship.OrderMoveTo(10000000, 0); // 10 km: long enough to reach and hold top speed

    for (int tick = 0; tick < 500; ++tick)
    {
      ship.Tick();
      Assert::IsTrue(ship.SpeedMillimetresPerTick() <= Frontier::Ship::MAX_SPEED_MILLIMETRES_PER_TICK);
      Assert::IsTrue(ship.SpeedMillimetresPerTick() >= 0);
    }
  }

  TEST_METHOD(ReachesTopSpeedInTheAdvertisedTime)
  {
    Frontier::Ship ship;
    ship.OrderMoveTo(10000000, 0);

    std::int32_t ticks = 0;
    while (ship.SpeedMillimetresPerTick() < Frontier::Ship::MAX_SPEED_MILLIMETRES_PER_TICK && ticks < 100)
    {
      ship.Tick();
      ++ticks;
    }

    // 1200 / 60 = 20 ticks, which is one second at 20 Hz. Ship.h says so; this is what checks it.
    Assert::AreEqual(20, ticks);
  }

  // A ship ordered somewhere behind it must slow down and turn rather than carve a wide arc away
  // from the target. FACING_TOLERANCE_TURNS16 is what produces that, and this is the behavior it
  // exists for.
  TEST_METHOD(SlowsDownBeforeTurningBack)
  {
    Frontier::Ship ship;
    ship.OrderMoveTo(10000000, 0);
    for (int tick = 0; tick < 40; ++tick)
    {
      ship.Tick(); // up to top speed, heading +X
    }
    Assert::AreEqual(Frontier::Ship::MAX_SPEED_MILLIMETRES_PER_TICK, ship.SpeedMillimetresPerTick());

    // Now send it back the other way.
    ship.OrderMoveTo(-10000000, 0);
    ship.Tick();
    Assert::IsTrue(ship.SpeedMillimetresPerTick() < Frontier::Ship::MAX_SPEED_MILLIMETRES_PER_TICK,
                   L"a ship pointed the wrong way should be slowing, not accelerating");
  }

  TEST_METHOD(ANewOrderReplacesTheOldOne)
  {
    Frontier::Ship ship;
    ship.OrderMoveTo(50000, 0);
    for (int tick = 0; tick < 5; ++tick)
    {
      ship.Tick();
    }

    ship.OrderMoveTo(0, 50000);
    TickUntilStopped(ship);

    Assert::AreEqual(0LL, ship.PositionXMillimetres());
    Assert::AreEqual(50000LL, ship.PositionZMillimetres());
  }

  // A target the ship is already on: it must finish immediately rather than jittering around it.
  TEST_METHOD(AnOrderToWhereItAlreadyIsCompletesAtOnce)
  {
    Frontier::Ship ship;
    ship.OrderMoveTo(0, 0);
    ship.Tick();

    Assert::IsFalse(ship.HasOrder());
    Assert::AreEqual(0LL, ship.PositionXMillimetres());
    Assert::AreEqual(0LL, ship.PositionZMillimetres());
    Assert::AreEqual(0, ship.SpeedMillimetresPerTick());
  }

  // R16: the same ticks from the same start give the same answer, to the millimetre. This is the
  // property the whole integer simulation exists for, so it is worth asserting rather than
  // assuming it follows from there being no float.
  TEST_METHOD(IsDeterministic)
  {
    auto run = []
    {
      Frontier::Ship ship;
      ship.OrderMoveTo(123456, -78901);
      for (int tick = 0; tick < 200; ++tick)
      {
        ship.Tick();
      }
      return std::tuple{ship.PositionXMillimetres(), ship.PositionZMillimetres(), ship.HeadingTurns16()};
    };

    Assert::IsTrue(run() == run());
  }

  TEST_METHOD(DistanceIsExactForKnownTriangles)
  {
    Assert::AreEqual(5LL, Frontier::Ship::DistanceMillimetres(3, 4));
    Assert::AreEqual(13LL, Frontier::Ship::DistanceMillimetres(-5, 12));
    Assert::AreEqual(0LL, Frontier::Ship::DistanceMillimetres(0, 0));
  }

  // ADR-004: the far end of the range saturates rather than overflowing. Unreachable in play, and
  // still not allowed to be undefined.
  TEST_METHOD(DistanceSaturatesRatherThanOverflowing)
  {
    constexpr std::int64_t HUGE_VALUE = 1LL << 40;
    Assert::AreEqual(std::numeric_limits<std::int64_t>::max(), Frontier::Ship::DistanceMillimetres(HUGE_VALUE, HUGE_VALUE));
  }
};

// The world is a thin thing -- it holds a ship and counts ticks -- but it is also the seam the
// server ticks through, so the shape of what it reports is worth pinning.
TEST_CLASS(WorldTests)
{
public:
  TEST_METHOD(CountsItsTicks)
  {
    Frontier::World world;
    Assert::AreEqual(0ULL, world.TickCount());

    for (int tick = 0; tick < 7; ++tick)
    {
      world.Tick();
    }

    Assert::AreEqual(7ULL, world.TickCount());
    Assert::AreEqual(7ULL, world.Snapshot().tick);
  }

  TEST_METHOD(AnOrderMovesTheShipItReportsOn)
  {
    Frontier::World world;
    world.ApplyOrder(Neuron::MoveToOrder{.targetXMillimetres = 40000, .targetZMillimetres = 0});

    for (int tick = 0; tick < 200 && world.PlayerShip().HasOrder(); ++tick)
    {
      world.Tick();
    }

    const Neuron::ShipState state = world.Snapshot();
    Assert::AreEqual(40000LL, state.positionXMillimetres);
    Assert::AreEqual(0LL, state.positionZMillimetres);
    Assert::AreEqual(0, state.speedMillimetresPerTick);
  }

  TEST_METHOD(SnapshotAgreesWithTheShip)
  {
    Frontier::World world;
    world.ApplyOrder(Neuron::MoveToOrder{.targetXMillimetres = 90000, .targetZMillimetres = 45000});
    for (int tick = 0; tick < 25; ++tick)
    {
      world.Tick();
    }

    const Neuron::ShipState state = world.Snapshot();
    Assert::AreEqual(world.PlayerShip().PositionXMillimetres(), state.positionXMillimetres);
    Assert::AreEqual(world.PlayerShip().PositionZMillimetres(), state.positionZMillimetres);
    Assert::AreEqual(world.PlayerShip().HeadingTurns16(), state.headingTurns16);
    Assert::AreEqual(world.PlayerShip().SpeedMillimetresPerTick(), state.speedMillimetresPerTick);
  }
};

} // namespace GameLogicTests
