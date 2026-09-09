#include "pch.h"
#include "CppUnitTest.h"

#include "Session.h"

#include <chrono>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

namespace
{

/// A simulation that does nothing but count, so that the Session's own behavior -- the thread,
/// the schedule, the order of orders against ticks -- can be tested without a game attached.
///
/// This is the second implementation of Neuron::Simulation that ADR-007 says the seam would get,
/// and it arrived exactly where that ADR expected: in a test.
class CountingSimulation final : public Neuron::Simulation
{
public:
  void ApplyOrder(const Neuron::MoveToOrder& _order) override
  {
    // Orders must be applied before the tick they arrived for. Recording the tick they landed on
    // is what lets the test below assert that.
    m_lastOrderTick.store(m_ticks.load(std::memory_order_relaxed), std::memory_order_relaxed);
    m_lastOrderX.store(_order.targetXMillimetres, std::memory_order_relaxed);
    m_orderCount.fetch_add(1, std::memory_order_relaxed);
  }

  void Tick() override
  {
    m_ticks.fetch_add(1, std::memory_order_relaxed);
  }

  [[nodiscard]] Neuron::ShipState Snapshot() const override
  {
    return Neuron::ShipState{.tick = m_ticks.load(std::memory_order_relaxed),
                             .positionXMillimetres = m_lastOrderX.load(std::memory_order_relaxed),
                             .positionZMillimetres = 0,
                             .speedMillimetresPerTick = 0,
                             .headingTurns16 = 0,
                             .reserved = 0};
  }

  [[nodiscard]] std::uint64_t Ticks() const noexcept
  {
    return m_ticks.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t OrderCount() const noexcept
  {
    return m_orderCount.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t LastOrderTick() const noexcept
  {
    return m_lastOrderTick.load(std::memory_order_relaxed);
  }

private:
  std::atomic<std::uint64_t> m_ticks = 0;
  std::atomic<std::uint64_t> m_orderCount = 0;
  std::atomic<std::uint64_t> m_lastOrderTick = 0;
  std::atomic<std::int64_t> m_lastOrderX = 0;
};

} // namespace

TEST_CLASS(SessionTests)
{
public:
  TEST_METHOD(TheTickRateIsTwentyHertz)
  {
    // Owner decision, MVP-01 section 2. Pinned because the client's interpolation window is
    // derived from it and the two have to agree.
    Assert::AreEqual(20LL, Neuron::Session::TICKS_PER_SECOND);
    Assert::AreEqual(50000LL, Neuron::Session::TICK_MICROSECONDS);
  }

  TEST_METHOD(TicksOnItsOwnThreadWithoutBeingDriven)
  {
    Neuron::LoopbackTransport transport;
    auto simulation = std::make_unique<CountingSimulation>();
    CountingSimulation* watched = simulation.get();

    Neuron::Session session;
    session.Start(std::move(simulation), transport);

    // Nothing here drives the session: the whole point is that it runs by itself (MVP-01
    // section 2). Half a second at 20 Hz is ten ticks; the bound is loose in both directions
    // because this is a wall clock on a machine doing other things.
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    session.Stop();

    const std::uint64_t ticks = watched->Ticks();
    Assert::IsTrue(ticks >= 5, (std::wstring(L"only ") + std::to_wstring(ticks) + L" ticks in half a second").c_str());
    Assert::IsTrue(ticks <= 30, (std::wstring(L"far too many ticks: ") + std::to_wstring(ticks)).c_str());
    Assert::AreEqual(ticks, session.TicksRun());
  }

  TEST_METHOD(ReplicatesAStateEveryTick)
  {
    Neuron::LoopbackTransport transport;
    Neuron::Session session;
    session.Start(std::make_unique<CountingSimulation>(), transport);

    std::this_thread::sleep_for(std::chrono::milliseconds{300});
    session.Stop();

    std::uint64_t stateCount = 0;
    std::uint64_t lastTick = 0;
    Neuron::ShipState state = {};
    while (transport.ReceiveState(state))
    {
      ++stateCount;
      Assert::IsTrue(state.tick > lastTick, L"states arrive in tick order and never repeat");
      lastTick = state.tick;
    }

    Assert::IsTrue(stateCount >= 3, L"the client end should have states waiting for it");
    Assert::AreEqual(session.TicksRun(), lastTick, L"the last state is from the last tick");
  }

  TEST_METHOD(AppliesOrdersBeforeTheTickTheyArriveFor)
  {
    Neuron::LoopbackTransport transport;
    auto simulation = std::make_unique<CountingSimulation>();
    CountingSimulation* watched = simulation.get();

    Neuron::Session session;
    session.Start(std::move(simulation), transport);

    transport.SendOrder({.targetXMillimetres = 4242, .targetZMillimetres = 0});
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    const std::uint64_t orderTick = watched->LastOrderTick();
    const std::uint64_t ticks = watched->Ticks();
    session.Stop();

    Assert::AreEqual(1ULL, watched->OrderCount());
    Assert::IsTrue(orderTick < ticks, L"the order was applied before a tick, not after the last one");
  }

  TEST_METHOD(DrainsEveryOrderThatArrivedInOneTick)
  {
    Neuron::LoopbackTransport transport;
    auto simulation = std::make_unique<CountingSimulation>();
    CountingSimulation* watched = simulation.get();

    // Queue them all before the session starts, so they are certainly waiting on the first tick.
    for (std::int64_t index = 0; index < 25; ++index)
    {
      transport.SendOrder({.targetXMillimetres = index, .targetZMillimetres = 0});
    }

    Neuron::Session session;
    session.Start(std::move(simulation), transport);
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    session.Stop();

    Assert::AreEqual(25ULL, watched->OrderCount(), L"a tick must drain the whole queue, not one order");
  }

  TEST_METHOD(StopIsSafeToCallTwiceAndWithoutStarting)
  {
    Neuron::Session neverStarted;
    neverStarted.Stop();
    neverStarted.Stop();

    Neuron::LoopbackTransport transport;
    Neuron::Session session;
    session.Start(std::make_unique<CountingSimulation>(), transport);
    session.Stop();
    session.Stop();
  }
};

} // namespace NeuronServerTests
