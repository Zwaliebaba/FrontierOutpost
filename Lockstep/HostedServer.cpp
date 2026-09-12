// HostedServer.cpp -- the server, on its own thread, in this process.

#include "pch.h"
#include "HostedServer.h"

#include "MatchLog.h"
#include "MatchSimulation.h"
#include "Session.h"
#include "TickSchedule.h"

#include <algorithm>
#include <chrono>

namespace Lockstep
{

namespace
{
/// How often the server thread wakes. At four locks a day this could be a second; sixty a second
/// keeps a locally-hosted client's own messages feeling immediate, and costs nothing measurable.
constexpr std::chrono::milliseconds POLL_INTERVAL{16};

/// The one wall clock on the server side, and it is here rather than inside anything (ADR-026).
///
/// UTC seconds since the epoch, which is what `TickSchedule` is defined over: a schedule anchored
/// on an instant that survives the process is what lets a restarted server owe exactly the locks
/// it slept through (ADR-042). A clock relative to process start would owe nothing after every
/// restart and lock at a different time of day each time.
[[nodiscard]] Neuron::Instant UtcNow()
{
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void LogRoster(Neuron::MatchLog& _log, const std::vector<std::optional<BotPolicy>>& _bots)
{
  // Named in the log, because a match's result means something different when three of the
  // empires were played by the machine and nothing else records which (ADR-030).
  for (std::size_t seat = 0; seat < _bots.size(); ++seat)
  {
    if (_bots[seat].has_value())
    {
      _log.Write(std::format("bot-seat player={} style={}", seat + 1, Describe(*_bots[seat])));
    }
  }
}
} // namespace

HostedServer::HostedServer(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath,
                           std::uint64_t _seed, const MatchRules& _rules, std::vector<std::optional<BotPolicy>> _bots)
{
  m_thread = std::thread(
    [this, _port, tokens = std::move(_tokens), store = std::move(_storePath), log = std::move(_logPath), _seed, rules = _rules,
     bots = std::move(_bots)]() mutable
    {
      Guarded(std::move(log),
              [&](Neuron::MatchLog& _log) { Run(_log, _port, std::move(tokens), std::move(store), _seed, rules, std::move(bots)); });
    });
}

HostedServer::HostedServer(std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath, std::string _logPath)
{
  m_thread =
    std::thread([this, _port, tokens = std::move(_tokens), store = std::move(_storePath), log = std::move(_logPath)]() mutable
                { Guarded(std::move(log), [&](Neuron::MatchLog& _log) { RunLobby(_log, _port, std::move(tokens), std::move(store)); }); });
}

HostedServer::HostedServer(std::uint16_t _port, Neuron::MatchStore::Contents _contents, std::string _storePath, std::string _logPath)
  : m_resumed(true)
{
  m_thread =
    std::thread([this, _port, contents = std::move(_contents), store = std::move(_storePath), log = std::move(_logPath)]() mutable
                { Guarded(std::move(log), [&](Neuron::MatchLog& _log) { RunResumed(_log, _port, contents, std::move(store)); }); });
}

void HostedServer::Begin(std::uint64_t _seed, const MatchRules& _rules, std::vector<std::optional<BotPolicy>> _bots)
{
  std::lock_guard<std::mutex> held{m_lobbyLock};
  m_beginSeed = _seed;
  m_beginRules = _rules;
  m_beginBots = std::move(_bots);
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

std::string HostedServer::Failure() const
{
  std::lock_guard<std::mutex> held{m_logLock};
  return m_failure;
}

void HostedServer::Fail(const std::string& _what)
{
  {
    std::lock_guard<std::mutex> held{m_logLock};
    m_failure = _what;
    m_log.push_back("FATAL " + _what);
  }
  m_failed.store(true);
}

void HostedServer::Guarded(std::string _logPath, const std::function<void(Neuron::MatchLog&)>& _body)
{
  // The instrumentation log (ADR-030). Opened on this thread and written from it, so it needs no
  // lock -- the same reason nothing else on this thread does. It outlives the body so that a fatal
  // thrown anywhere in it can still be written down.
  Neuron::MatchLog log{std::move(_logPath)};

  try
  {
    _body(log);
  }
  catch (const std::exception& error)
  {
    log.Write(std::string("FATAL ") + error.what());
    Fail(error.what());
  }
}

void HostedServer::Serve(Neuron::MatchServer& _server, Neuron::MatchLog& _log)
{
  m_port.store(_server.Port());
  m_listening.store(_server.Listening());
  if (!_server.Listening())
  {
    Neuron::Fatal("Could not listen on port {}. Another program holds it, or a connection from a previous run is still draining.",
                  _server.Port());
  }

  while (m_running.load())
  {
    (void)_server.Poll(UtcNow());

    std::vector<std::string> lines = _server.TakeLog();
    if (!lines.empty())
    {
      _log.Write(lines);

      std::lock_guard<std::mutex> held{m_logLock};
      for (std::string& line : lines)
      {
        m_log.push_back(std::move(line));
      }
    }

    std::this_thread::sleep_for(POLL_INTERVAL);
  }
}

void HostedServer::Run(Neuron::MatchLog& _log, std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath,
                       std::uint64_t _seed, const MatchRules& _rules, std::vector<std::optional<BotPolicy>> _bots)
{
  const auto botCount = static_cast<std::size_t>(
    std::count_if(_bots.begin(), _bots.end(), [](const std::optional<BotPolicy>& _bot) { return _bot.has_value(); }));
  _log.Write(std::format("match-start seed={} players={} bots={} tick-seconds={} length={}", _seed, _rules.playerCount, botCount,
                         _rules.tickIntervalSeconds, _rules.matchLengthTicks));
  LogRoster(_log, _bots);

  // Everything below is created on this thread and destroyed on it. The simulation, the session and
  // the server never leave, which is what makes the absence of a lock correct rather than lucky.
  auto simulation = std::make_unique<MatchSimulation>(_rules, _seed, std::move(_bots));
  auto session = std::make_unique<Neuron::Session>(std::move(simulation), Neuron::TickSchedule{UtcNow(), _rules.tickIntervalSeconds},
                                                   std::move(_storePath), _tokens);
  Neuron::MatchServer server{std::move(session), _port, std::move(_tokens)};
  m_started.store(true);

  Serve(server, _log);
}

void HostedServer::RunResumed(Neuron::MatchLog& _log, std::uint16_t _port, const Neuron::MatchStore::Contents& _contents,
                              std::string _storePath)
{
  // The replay is the load (ADR-024): a fresh simulation from the stored configuration, every
  // locked turn resolved through it in order, and the hash it ends on compared with the one the
  // store was written with. A mismatch is not a state to recover from.
  std::unique_ptr<MatchSimulation> simulation = MatchSimulation::FromConfiguration(_contents.configuration);
  std::unique_ptr<Neuron::Session> session = Neuron::Session::Resume(std::move(simulation), _contents, std::move(_storePath));
  if (session == nullptr)
  {
    Neuron::Fatal("The match store does not replay to the hash it was written with: the simulation has changed under a live match "
                  "(ADR-024). Move the store aside to start a new match.");
  }

  _log.Write(std::format("match-resumed tick={} players={} tick-seconds={} started-at={}", session->Match().Tick(),
                         session->Match().PlayerCount(), _contents.intervalSeconds, _contents.startedAt));

  Neuron::MatchServer server{std::move(session), _port, _contents.tokens};
  m_started.store(true);

  Serve(server, _log);
}

void HostedServer::RunLobby(Neuron::MatchLog& _log, std::uint16_t _port, std::vector<std::string> _tokens, std::string _storePath)
{
  // The log opens with the lobby rather than with the match, because who arrived and when is part
  // of the record even for a match that never starts (ADR-030's login curve begins here).
  _log.Write(std::format("lobby-open seats={}", _tokens.size()));

  const std::size_t seatCount = _tokens.size();
  const std::vector<std::string> tokens = _tokens;
  Neuron::MatchServer server{_port, std::move(_tokens)};

  m_port.store(server.Port());
  m_listening.store(server.Listening());
  if (!server.Listening())
  {
    Neuron::Fatal("Could not listen on port {}. Another program holds it, or a connection from a previous run is still draining.", _port);
  }

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
      std::vector<std::optional<BotPolicy>> bots;
      {
        std::lock_guard<std::mutex> held{m_lobbyLock};
        begin = m_beginRequested;
        seed = m_beginSeed;
        rules = m_beginRules;
        bots = std::move(m_beginBots);
        m_beginRequested = false;
      }

      if (begin)
      {
        const auto botCount = static_cast<std::size_t>(
          std::count_if(bots.begin(), bots.end(), [](const std::optional<BotPolicy>& _bot) { return _bot.has_value(); }));
        _log.Write(std::format("match-start seed={} players={} bots={} tick-seconds={} length={}", seed, rules.playerCount, botCount,
                               rules.tickIntervalSeconds, rules.matchLengthTicks));
        LogRoster(_log, bots);

        // The seats that play are the first `playerCount` tokens, in seat order (SeatsPage), and
        // they go into the store with the match so a restart admits the same people (ADR-042).
        std::vector<std::string> playing(tokens.begin(), tokens.begin() + std::min<std::size_t>(rules.playerCount, tokens.size()));

        auto simulation = std::make_unique<MatchSimulation>(rules, seed, std::move(bots));
        server.Begin(std::make_unique<Neuron::Session>(std::move(simulation), Neuron::TickSchedule{UtcNow(), rules.tickIntervalSeconds},
                                                       _storePath, std::move(playing)));
        m_started.store(true);
      }
    }

    (void)server.Poll(UtcNow());

    // Published every poll, so a lobby screen a frame behind is the worst it gets.
    {
      std::lock_guard<std::mutex> held{m_lobbyLock};
      m_seatsConnected = server.SeatsConnected();
      m_seatsConnected.resize(seatCount, false);
    }

    std::vector<std::string> lines = server.TakeLog();
    if (!lines.empty())
    {
      _log.Write(lines);

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
