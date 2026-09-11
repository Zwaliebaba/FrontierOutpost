// MatchServer.cpp -- one match, on a socket.
//
// The whole of the network side, and it is small because everything hard is somewhere else:
// framing is `FrameStream`, message shapes are `Protocol`, when to resolve is `TickSchedule`, and
// what a tick means is behind `Simulation`. What is left here is who is connected and whether to
// believe them.
//
// NOTHING IN THIS FILE DECODES AN ORDER SET. It routes bytes. A server that could read an order
// could be tempted to act on one (ADR-025).

#include "pch.h"
#include "MatchServer.h"

namespace Neuron
{

namespace
{
/// Read in bites this size. A snapshot is a few kilobytes, so one poll usually drains a message.
constexpr std::size_t READ_CHUNK = 4096;
} // namespace

MatchServer::MatchServer(std::unique_ptr<Session> _session, std::uint16_t _port, std::vector<std::string> _tokens)
  : m_session(std::move(_session)),
    m_listener(Socket::Listen(_port)),
    m_tokens(std::move(_tokens))
{
  ASSERT_TEXT(m_session != nullptr, L"A server without a session has no match to serve.");

  if (!m_listener.Valid())
  {
    Log("listen failed");
    return;
  }
  Log(std::format("listening on port {}", m_listener.Port()));
}

std::int32_t MatchServer::PlayerFor(const std::string& _token) const
{
  for (std::size_t index = 0; index < m_tokens.size(); ++index)
  {
    if (m_tokens[index] == _token)
    {
      return static_cast<std::int32_t>(index);
    }
  }
  return -1;
}

void MatchServer::Log(std::string _line)
{
  // Stamped with the tick rather than a wall clock. The server has no clock of its own -- it is
  // handed an instant (ADR-026) -- and the tick is what a Phase 0 reader is correlating against
  // anyway. The caller adds a timestamp if it wants one.
  m_log.push_back(std::format("T{} {}", m_session->Match().Tick(), _line));
}

std::vector<std::string> MatchServer::TakeLog()
{
  std::vector<std::string> taken;
  taken.swap(m_log);
  return taken;
}

std::int32_t MatchServer::Connected() const
{
  std::int32_t count = 0;
  for (const Connection& connection : m_connections)
  {
    count += connection.player >= 0 && !connection.closing ? 1 : 0;
  }
  return count;
}

void MatchServer::Accept()
{
  // One per poll is enough at six players and keeps a flood of connections from starving the rest
  // of the loop.
  Socket accepted = m_listener.Accept();
  if (!accepted.Valid())
  {
    return;
  }

  Connection connection;
  connection.socket = std::move(accepted);
  m_connections.push_back(std::move(connection));
  Log("a client connected");
}

void MatchServer::Read(Connection& _connection)
{
  std::array<std::uint8_t, READ_CHUNK> buffer = {};

  for (;;)
  {
    const std::int32_t read = _connection.socket.Receive(buffer);
    if (read < 0)
    {
      _connection.closing = true;
      return;
    }
    if (read == 0)
    {
      return;
    }

    if (!_connection.incoming.Feed(std::span{buffer.data(), static_cast<std::size_t>(read)}))
    {
      // A frame length past anything legitimate. The stream is no longer aligned to anything, so
      // there is nothing to do but drop the connection.
      Log("dropped a client for a malformed frame");
      _connection.closing = true;
      return;
    }

    std::vector<std::uint8_t> payload;
    while (_connection.incoming.Take(payload))
    {
      Handle(_connection, payload);
      if (_connection.closing)
      {
        return;
      }
    }
  }
}

void MatchServer::Handle(Connection& _connection, std::span<const std::uint8_t> _payload)
{
  const MessageKind kind = Protocol::KindOf(_payload);

  // Nothing but Hello is accepted before a Hello. An unidentified connection that could submit
  // orders would be an unidentified connection that could play somebody else's turn.
  if (_connection.player < 0 && kind != MessageKind::Hello)
  {
    Send(_connection, Protocol::EncodeRefused(RefusalReason::Malformed));
    _connection.closing = true;
    return;
  }

  switch (kind)
  {
  case MessageKind::Hello:
  {
    std::string token;
    if (!Protocol::DecodeHello(_payload, token))
    {
      Send(_connection, Protocol::EncodeRefused(RefusalReason::Malformed));
      _connection.closing = true;
      return;
    }

    const std::int32_t player = PlayerFor(token);
    if (player < 0)
    {
      // The token is not logged, ever. It is the only secret in this protocol and a log is the
      // likeliest place for it to end up somewhere it should not.
      Log("refused an unknown token");
      Send(_connection, Protocol::EncodeRefused(RefusalReason::UnknownToken));
      _connection.closing = true;
      return;
    }

    for (const Connection& other : m_connections)
    {
      if (&other != &_connection && other.player == player && !other.closing)
      {
        Log(std::format("refused player {}: already connected", player));
        Send(_connection, Protocol::EncodeRefused(RefusalReason::AlreadyConnected));
        _connection.closing = true;
        return;
      }
    }

    _connection.player = player;
    Log(std::format("login player {}", player));

    Send(_connection, Protocol::EncodeWelcome(player, m_session->Match().Tick(), 0));
    // The state follows immediately, so a client that has just connected has something to draw
    // without waiting up to six hours for the next lock.
    PushState(_connection, 0);
    return;
  }

  case MessageKind::Orders:
  {
    std::vector<std::uint8_t> orders;
    if (!Protocol::DecodeOrders(_payload, orders))
    {
      Log(std::format("player {} sent a malformed order message", _connection.player));
      Send(_connection, Protocol::EncodeRefused(RefusalReason::Malformed));
      _connection.closing = true;
      return;
    }

    // Straight through, unread. Whether it is a legal order set is the game's to decide and it
    // will say so in the digest.
    (void)m_session->Submit(_connection.player, orders);
    m_session->MarkPresent(_connection.player);
    Log(std::format("orders from player {}", _connection.player));
    return;
  }

  case MessageKind::Ping:
    // Presence, which is a fact about being seen rather than about submitting. This is the message
    // that keeps a player out of custody while they sit and think.
    m_session->MarkPresent(_connection.player);
    return;

  case MessageKind::Welcome:
  case MessageKind::Refused:
  case MessageKind::State:
  default:
    // Messages only a server sends. A client sending one is confused or is not a client.
    Send(_connection, Protocol::EncodeRefused(RefusalReason::Malformed));
    _connection.closing = true;
    return;
  }
}

void MatchServer::Send(Connection& _connection, std::span<const std::uint8_t> _payload)
{
  const std::vector<std::uint8_t> framed = FrameStream::Frame(_payload);
  _connection.outgoing.insert(_connection.outgoing.end(), framed.begin(), framed.end());
}

void MatchServer::Flush(Connection& _connection)
{
  while (!_connection.outgoing.empty())
  {
    const std::int32_t written = _connection.socket.Send(_connection.outgoing);
    if (written < 0)
    {
      _connection.closing = true;
      return;
    }
    if (written == 0)
    {
      // The send buffer is full. The rest waits for the next poll rather than spinning -- at four
      // messages a day this happens when a client has stopped reading, and the queue is the place
      // that shows it.
      return;
    }
    _connection.outgoing.erase(_connection.outgoing.begin(), _connection.outgoing.begin() + written);
  }
}

void MatchServer::PushState(Connection& _connection, Instant _now)
{
  if (_connection.player < 0)
  {
    return;
  }

  Send(_connection, Protocol::EncodeState(m_session->Match().Tick(), m_session->SecondsUntilNextLock(_now),
                                          m_session->SnapshotFor(_connection.player), m_session->DigestFor(_connection.player)));
}

std::uint32_t MatchServer::Poll(Instant _now)
{
  Accept();

  for (Connection& connection : m_connections)
  {
    if (!connection.closing)
    {
      Read(connection);
    }
  }

  const std::uint32_t resolved = m_session->Advance(_now);
  if (resolved > 0)
  {
    Log(std::format("resolved {} tick(s)", resolved));
    if (!m_session->Persisted())
    {
      Log("WARNING could not write the match store");
    }
  }

  // The game's own instrumentation, taken every poll rather than only after a lock -- a tick that
  // resolved and whose events were left behind is a gap in the record with no way to notice it.
  for (std::string& line : m_session->TakeEvents())
  {
    m_log.push_back(std::move(line));
  }

  // State goes out when the tick moved, and once when a match first has connections -- so a client
  // that joined mid-tick is not looking at nothing until the next lock.
  const bool tickMoved = resolved > 0 || !m_pushedOnce || m_session->Match().Tick() != m_pushedTick;
  if (tickMoved)
  {
    for (Connection& connection : m_connections)
    {
      if (connection.player >= 0 && !connection.closing)
      {
        PushState(connection, _now);
      }
    }
    m_pushedTick = m_session->Match().Tick();
    m_pushedOnce = true;
  }

  for (Connection& connection : m_connections)
  {
    Flush(connection);
  }

  // Closed connections are removed last, so nothing above holds a reference into a vector that is
  // about to move.
  for (const Connection& connection : m_connections)
  {
    if (connection.closing && connection.player >= 0)
    {
      Log(std::format("player {} disconnected", connection.player));
    }
  }
  std::erase_if(m_connections, [](const Connection& _connection) { return _connection.closing; });

  return resolved;
}

} // namespace Neuron
