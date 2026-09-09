// Session.cpp -- the server thread and the fixed tick.

#include "pch.h"
#include "Session.h"

#include <chrono>

namespace Neuron
{

Session::~Session()
{
  Stop();
}

void Session::Start(std::unique_ptr<Simulation> _simulation, LoopbackTransport& _transport)
{
  ASSERT_TEXT(_simulation != nullptr, L"A session with no simulation has nothing to tick.");
  ASSERT_TEXT(!m_thread.joinable(), L"This session is already running.");

  m_simulation = std::move(_simulation);
  m_transport = &_transport;
  m_running.store(true, std::memory_order_relaxed);
  m_thread = std::thread{[this] { Run(); }};
}

void Session::Stop() noexcept
{
  m_running.store(false, std::memory_order_relaxed);
  if (m_thread.joinable())
  {
    m_thread.join();
  }
}

void Session::Run()
{
  // The wall clock is used to decide WHEN to tick and never inside one. Nothing the simulation
  // computes depends on it, which is the distinction R16 draws: the tick is the clock for the
  // game, and this is only the thing that paces it against real time.
  //
  // The next deadline is advanced by exactly one tick each time rather than measured from now, so
  // a tick that runs long is made up by the next one instead of the schedule drifting.
  auto nextTick = std::chrono::steady_clock::now();

  while (m_running.load(std::memory_order_relaxed))
  {
    // Orders first, so that an order and the tick it takes effect on are never ambiguous. The
    // whole queue is drained: a client that sent three clicks in one tick meant all three, and
    // the last one wins by being applied last.
    MoveToOrder order = {};
    while (m_transport->ReceiveOrder(order))
    {
      m_simulation->ApplyOrder(order);
    }

    m_simulation->Tick();
    m_ticksRun.fetch_add(1, std::memory_order_relaxed);

    m_transport->SendState(m_simulation->Snapshot());

    nextTick += std::chrono::microseconds{TICK_MICROSECONDS};
    const auto now = std::chrono::steady_clock::now();
    if (nextTick > now)
    {
      std::this_thread::sleep_until(nextTick);
    }
    else
    {
      // The tick took longer than a tick. Rather than trying to catch up -- which makes a slow
      // machine run the simulation faster and then fall further behind -- the schedule is reset
      // to now and the lost time is simply lost. The MVP will never see this; an MMO will, and
      // the honest thing is for it to show up as a late state rather than as a burst of ticks.
      nextTick = now;
    }
  }
}

} // namespace Neuron
