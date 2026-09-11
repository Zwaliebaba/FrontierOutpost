#pragma once

#include "MatchServer.h"

#include "MatchRules.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Frontier
{

/// A match server, on its own thread, inside this process.
///
/// **The server and its session live entirely on this thread and nothing else touches them.** That
/// is the whole concurrency story: the client shares no state with the server, only a socket, so
/// there is no lock anywhere near the simulation and resolution stays single-threaded and
/// deterministic (ADR-028).
///
/// It is used by *host and play*, where the player running it is also playing, and by `--serve`,
/// where nobody is.
class HostedServer
{
public:
  /// Starts a match and begins listening. `_port` of zero asks the OS for one; `Port()` says which.
  HostedServer(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath, std::uint64_t _seed,
               const MatchRules& _rules);
  ~HostedServer();

  HostedServer(const HostedServer&) = delete;
  HostedServer& operator=(const HostedServer&) = delete;
  HostedServer(HostedServer&&) = delete;
  HostedServer& operator=(HostedServer&&) = delete;

  [[nodiscard]] bool Listening() const noexcept
  {
    return m_listening.load();
  }
  [[nodiscard]] std::uint16_t Port() const noexcept
  {
    return m_port.load();
  }

  /// Everything the server logged, taken and cleared. Safe from any thread.
  [[nodiscard]] std::vector<std::string> TakeLog();

  void Stop() noexcept;

private:
  void Run(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath, std::uint64_t _seed,
           MatchRules _rules);

  std::thread m_thread;
  std::atomic<bool> m_running{true};
  std::atomic<bool> m_listening{false};
  std::atomic<std::uint16_t> m_port{0};

  /// The only thing two threads touch, and it is a vector of strings behind a mutex rather than
  /// anything clever -- a few lines a tick is not a performance problem.
  std::mutex m_logLock;
  std::vector<std::string> m_log;
};

} // namespace Frontier
