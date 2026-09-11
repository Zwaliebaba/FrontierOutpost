// HostedServer.cpp -- the server, on its own thread, in this process.

#include "pch.h"
#include "HostedServer.h"

#include "MatchLog.h"
#include "MatchSimulation.h"
#include "Session.h"
#include "TickSchedule.h"

#include <chrono>

namespace Lockstep
{

namespace
{
/// How often the server thread wakes. At four locks a day this could be a second; sixty a second
/// keeps a locally-hosted client's own messages feeling immediate, and costs nothing measurable.
constexpr std::chrono::milliseconds POLL_INTERVAL{16};
} // namespace

HostedServer::HostedServer(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath,
                           std::uint64_t _seed, const MatchRules& _rules)
{
  m_thread = std::thread([this, _port, tokens = std::move(_tokens), store = std::move(_storePath), log = std::move(_logPath), _seed,
                          rules = _rules]() mutable { Run(_port, std::move(tokens), std::move(store), std::move(log), _seed, rules); });
}

HostedServer::HostedServer(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath)
{
  m_thread = std::thread([this, _port, tokens = std::move(_tokens), store = std::move(_storePath), log = std::move(_logPath)]() mutable
                         { RunLobby(_port, std::move(tokens), std::move(store), std::move(log)); });
}

void HostedServer::Begin(std::uint64_t _seed, const MatchRules& _rules)
{
  std::lock_guard<std::mutex> held{m_lobbyLock};
  m_beginSeed = _seed;
  m_beginRules = _rules;
  m_beginRequested = true;
}

std::vector<bool> HostedServer::SeatsConnected() const
{
  std::lock_guard<std::mutex> held{m_lobbyLock};
  return m_seatsConnected;
}

HostedServer::~HostedServer()
{
  Stop();
  if (m_thread.joinable())
  {
    m_thread.join();
  }
}

void HostedServer::Stop() noexcept
{
  m_running.store(false);
}

std::vector<std::string> HostedServer::TakeLog()
{
  std::lock_guard<std::mutex> held{m_logLock};
  std::vector<std::string> taken;
  taken.swap(m_log);
  return taken;
}

void HostedServer::Run(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath,
                       std::uint64_t _seed, MatchRules _rules)
{
  const MatchRules rules = _rules;

  // The instrumentation log (ADR-030). Opened on this thread and written from it, so it needs no
  // lock either -- the same reason nothing else here does.
  Neuron::MatchLog log{std::move(_logPath)};
  log.Write(std::format("match-start seed={} players={} tick-seconds={} length={}", _seed, rules.playerCount, rules.tickIntervalSeconds,
                        rules.matchLengthTicks));

  // Everything below is created on this thread and destroyed on it. The simulation, the session and
  // the server never leave, which is what makes the absence of a lock correct rather than lucky.
  auto simulation = std::make_unique<MatchSimulation>(rules, _seed);
  auto session =
    std::make_unique<Neuron::Session>(std::move(simulation), Neuron::TickSchedule{0, rules.tickIntervalSeconds}, std::move(_storePath));
  Neuron::MatchServer server{std::move(session), _port, std::move(_tokens)};

  m_port.store(server.Port());
  m_listening.store(server.Listening());

  const auto startedAt = std::chrono::steady_clock::now();

  while (m_running.load())
  {
    // The one clock on the server side, and it is here rather than inside anything (ADR-026).
    const auto now =
      static_cast<Neuron::Instant>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startedAt).count());

    (void)server.Poll(now);

    std::vector<std::string> lines = server.TakeLog();
    if (!lines.empty())
    {
      log.Write(lines);

      std::lock_guard<std::mutex> held{m_logLock};
      for (std::string& line : lines)
      {
        m_log.push_back(std::move(line));
      }
    }

    std::this_thread::sleep_for(POLL_INTERVAL);
  }
}

void HostedServer::RunLobby(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath)
{
  // The log opens with the lobby rather than with the match, because who arrived and when is part
  // of the record even for a match that never starts (ADR-030's login curve begins here).
  Neuron::MatchLog log{std::move(_logPath)};
  log.Write(std::format("lobby-open seats={}", _tokens.size()));

  const std::size_t seatCount = _tokens.size();
  Neuron::MatchServer server{_port, std::move(_tokens)};

  m_port.store(server.Port());
  m_listening.store(server.Listening());

  const auto startedAt = std::chrono::steady_clock::now();
  std::string storePath = std::move(_storePath);

  while (m_running.load())
  {
    // ---- The match, when the host asks for one -----------------------------------------------
    //
    // Built HERE, on the server thread, from a request left under a lock. The simulation never
    // crosses a thread: what crosses is a seed and a struct of numbers (ADR-028).
    if (!server.Started())
    {
      bool begin = false;
      std::uint64_t seed = 0;
      MatchRules rules;
      {
        std::lock_guard<std::mutex> held{m_lobbyLock};
        begin = m_beginRequested;
        seed = m_beginSeed;
        rules = m_beginRules;
        m_beginRequested = false;
      }

      if (begin)
      {
        log.Write(std::format("match-start seed={} players={} tick-seconds={} length={}", seed, rules.playerCount,
                              rules.tickIntervalSeconds, rules.matchLengthTicks));

        auto simulation = std::make_unique<MatchSimulation>(rules, seed);
        server.Begin(
          std::make_unique<Neuron::Session>(std::move(simulation), Neuron::TickSchedule{0, rules.tickIntervalSeconds}, storePath));
        m_started.store(true);
      }
    }

    const auto now =
      static_cast<Neuron::Instant>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startedAt).count());
    (void)server.Poll(now);

    // Published every poll, so a lobby screen a frame behind is the worst it gets.
    {
      std::lock_guard<std::mutex> held{m_lobbyLock};
      m_seatsConnected = server.SeatsConnected();
      m_seatsConnected.resize(seatCount, false);
    }

    std::vector<std::string> lines = server.TakeLog();
    if (!lines.empty())
    {
      log.Write(lines);

      std::lock_guard<std::mutex> held{m_logLock};
      for (std::string& line : lines)
      {
        m_log.push_back(std::move(line));
      }
    }

    std::this_thread::sleep_for(POLL_INTERVAL);
  }
}

} // namespace Lockstep
