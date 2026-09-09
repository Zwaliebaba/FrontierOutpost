#pragma once

#include "LoopbackTransport.h"
#include "Simulation.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace Neuron
{

/// The authoritative loop: owns a simulation, ticks it on a fixed schedule, and replicates it.
///
/// It runs on its own thread from the first commit, not eventually (MVP-01 section 2). Same-thread
/// is not an option even as a stepping stone, because a client that can call Tick() is a client
/// that will, and the seam that lets it is the one that never gets removed. Nothing here touches
/// D3D12 and nothing here blocks the client.
///
/// What it ticks is a Simulation, not a World: NeuronServer references NeuronCore and nothing
/// else, and the game's rules live on the other side of that seam (ADR-007).
class Session
{
public:
  /// 20 Hz, a 50 ms tick. Owner decision, 2026-09-09 (MVP-01 section 2). Recorded as a decision
  /// rather than a measurement -- it was not arrived at by timing anything.
  static constexpr std::int64_t TICKS_PER_SECOND = 20;
  static constexpr std::int64_t TICK_MICROSECONDS = 1000000 / TICKS_PER_SECOND;

  Session() = default;
  ~Session();

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(Session&&) = delete;

  /// Takes the simulation and starts ticking. The transport must outlive the Session.
  void Start(std::unique_ptr<Simulation> _simulation, LoopbackTransport& _transport);

  /// Asks the thread to finish the tick it is on and stop, then joins it. Called by the
  /// destructor; safe to call twice.
  void Stop() noexcept;

  /// Ticks run since Start. Read from the client thread for diagnostics, so it is atomic; nothing
  /// in the simulation depends on it.
  [[nodiscard]] std::uint64_t TicksRun() const noexcept
  {
    return m_ticksRun.load(std::memory_order_relaxed);
  }

private:
  void Run();

  std::unique_ptr<Simulation> m_simulation;
  LoopbackTransport* m_transport = nullptr;
  std::thread m_thread;
  std::atomic<bool> m_running = false;
  std::atomic<std::uint64_t> m_ticksRun = 0;
};

} // namespace Neuron
