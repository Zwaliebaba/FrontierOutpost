#pragma once

#include "MatchLog.h"
#include "MatchServer.h"

#include "BotPolicy.h"
#include "MatchRules.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Lockstep
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
               const MatchRules& _rules, std::vector<std::optional<BotPolicy>> _bots = {});

  /// Opens a LOBBY: listening, seats issued, no match. `Begin` starts the match later.
  ///
  /// **This is the order a player expects** -- log in, then start a game -- and the only order in
  /// which "how many are playing" can be answered before the galaxy is generated for that many.
  HostedServer(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath);

  /// Starts the match, from another thread.
  ///
  /// **Queued rather than done here.** The server and its session live on the server thread and
  /// nothing else touches them (ADR-028); that is the whole concurrency story and it is worth more
  /// than the convenience of constructing a simulation on the caller's thread. This leaves the
  /// request under a lock and the server thread picks it up on its next poll.
  ///
  /// `_bots` is one entry per seat, in seat order; an empty entry is a person's seat. It is copied
  /// across the lock with the rules, because it is part of the match's shape and not part of its
  /// state -- the simulation is built from it on the far side (ADR-037).
  void Begin(std::uint64_t _seed, const MatchRules& _rules, std::vector<std::optional<BotPolicy>> _bots);

  /// True once the match has actually started, which is a poll or two after `Begin`.
  [[nodiscard]] bool Started() const noexcept
  {
    return m_started.load();
  }

  /// Which seats have somebody on them. Published by the server thread every poll.
  [[nodiscard]] std::vector<bool> SeatsConnected() const;
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

  /// Whether the server thread has stopped on a fatal, and what it said.
  ///
  /// An exception on the server thread has no composition root to reach: left alone it is
  /// `std::terminate`, which on a headless server is a process that vanished with no line in any
  /// log. The thread catches it, writes it to the match log, and leaves it here for whoever owns the
  /// object to report and act on. Safe from any thread.
  [[nodiscard]] bool Failed() const noexcept
  {
    return m_failed.load();
  }
  [[nodiscard]] std::string Failure() const;

  void Stop() noexcept;

private:
  /// Records a fatal from the server thread. The thread ends after this.
  void Fail(const std::string& _what);
  void Run(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath, std::uint64_t _seed,
           MatchRules _rules, std::vector<std::optional<BotPolicy>> _bots);

  /// The lobby's loop: listen, seat people, and watch for a `Begin`. Opens the log and catches
  /// whatever the guarded half throws, so the log can record it.
  void RunLobby(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath);
  void RunLobbyGuarded(Neuron::MatchLog& _log, std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath);

  std::thread m_thread;
  std::atomic<bool> m_running{true};
  std::atomic<bool> m_listening{false};
  std::atomic<std::uint16_t> m_port{0};

  /// The only thing two threads touch, and it is a vector of strings behind a mutex rather than
  /// anything clever -- a few lines a tick is not a performance problem.
  std::mutex m_logLock;
  std::vector<std::string> m_log;
  std::string m_failure;
  std::atomic<bool> m_failed{false};

  /// The lobby's shared state, and the only other thing two threads touch. Same discipline as the
  /// log: a mutex and plain data, because a few seats a poll is not a performance problem.
  mutable std::mutex m_lobbyLock;
  std::vector<bool> m_seatsConnected;
  bool m_beginRequested = false;
  std::uint64_t m_beginSeed = 0;
  MatchRules m_beginRules;
  std::vector<std::optional<BotPolicy>> m_beginBots;

  std::atomic<bool> m_started{false};
};

} // namespace Lockstep
