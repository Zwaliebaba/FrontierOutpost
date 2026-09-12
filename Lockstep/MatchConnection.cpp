// MatchConnection.cpp -- the client's end of the wire.

#include "pch.h"
#include "MatchConnection.h"

#include <array>

namespace Lockstep
{

namespace
{
/// How long to wait between reconnection attempts. Long enough not to hammer a server that is
/// restarting, short enough that a player who closed their lid for a minute is back before the next
/// hourly lock.
constexpr double RECONNECT_INTERVAL_SECONDS = 2.0;
} // namespace

bool MatchConnection::Open(const std::string& _host, std::uint16_t _port, const std::string& _token)
{
  // Remembered, so a reconnect needs nothing from the caller and cannot use a different token by
  // accident -- which would make the player a different empire.
  m_host = _host;
  m_port = _port;
  m_token = _token;

  m_socket = Neuron::Socket::Connect(_host, _port);
  if (!m_socket.Valid())
  {
    return false;
  }

  m_incoming.Reset();
  m_outgoing.clear();
  m_status = Status::Connecting;
  m_refusal = Neuron::RefusalReason::None;
  m_connectDeadline = 0.0;
  return true;
}

bool MatchConnection::Settle(double _secondsSinceStart)
{
  // The deadline is set on the first poll rather than in `Open`, which has no clock and should not
  // grow one: the caller's seconds are the only clock this class has ever been told about.
  if (m_connectDeadline == 0.0)
  {
    m_connectDeadline = _secondsSinceStart + CONNECT_DEADLINE_SECONDS;
  }

  switch (m_socket.Progress())
  {
  case Neuron::Socket::Connection::Ready:
    m_status = Status::Greeting;
    m_connectDeadline = 0.0;

    // The hello waits for the handshake. Queuing it in `Open` would have been queuing bytes for a
    // peer that had not agreed to exist yet.
    Send(Neuron::Protocol::EncodeHello(m_token));
    return true;

  case Neuron::Socket::Connection::Failed:
    m_status = Status::Lost;
    m_socket.Close();
    m_connectDeadline = 0.0;
    return false;

  case Neuron::Socket::Connection::Pending:
  default:
    if (_secondsSinceStart >= m_connectDeadline)
    {
      m_status = Status::Lost;
      m_socket.Close();
      m_connectDeadline = 0.0;
    }
    return false;
  }
}

void MatchConnection::Reset() noexcept
{
  m_socket.Close();
  m_incoming.Reset();
  m_outgoing.clear();
  m_status = Status::Idle;
  m_refusal = Neuron::RefusalReason::None;
  m_connectDeadline = 0.0;
  m_player = -1;
  m_snapshot.clear();
  m_digests.clear();
  m_fresh = false;
}

void MatchConnection::Send(std::span<const std::uint8_t> _payload)
{
  const std::vector<std::uint8_t> framed = Neuron::FrameStream::Frame(_payload);
  m_outgoing.insert(m_outgoing.end(), framed.begin(), framed.end());
}

void MatchConnection::SendOrders(std::span<const std::uint8_t> _orderSet)
{
  if (m_status == Status::Playing)
  {
    Send(Neuron::Protocol::EncodeOrders(_orderSet));
  }
}

void MatchConnection::SendPing()
{
  if (m_status == Status::Playing)
  {
    Send(Neuron::Protocol::EncodePing());
  }
}

bool MatchConnection::TakeFreshState() noexcept
{
  const bool fresh = m_fresh;
  m_fresh = false;
  return fresh;
}

void MatchConnection::Handle(std::span<const std::uint8_t> _payload)
{
  switch (Neuron::Protocol::KindOf(_payload))
  {
  case Neuron::MessageKind::Welcome:
    if (Neuron::Protocol::DecodeWelcome(_payload, m_player, m_tick, m_secondsToLock))
    {
      m_status = Status::Playing;
    }
    return;

  case Neuron::MessageKind::Refused:
    (void)Neuron::Protocol::DecodeRefused(_payload, m_refusal);
    m_status = Status::Refused;
    m_socket.Close();
    return;

  case Neuron::MessageKind::State:
    if (Neuron::Protocol::DecodeState(_payload, m_tick, m_secondsToLock, m_snapshot, m_digests))
    {
      m_fresh = true;
    }
    return;

  case Neuron::MessageKind::Hello:
  case Neuron::MessageKind::Orders:
  case Neuron::MessageKind::Ping:
  default:
    // Messages only a client sends. A server sending one is not a server this client understands,
    // and continuing to trust the stream would be a guess.
    m_status = Status::Lost;
    m_socket.Close();
    return;
  }
}

void MatchConnection::Pump(double _secondsSinceStart)
{
  // ---- Coming back ---------------------------------------------------------------------------
  //
  // A refusal is final -- an unknown token will still be unknown in three seconds -- but a lost
  // connection is not. Attempts are spaced rather than spun on, because a client reconnecting sixty
  // times a second to a server that is down is a client nobody can debug next to.
  if (m_status == Status::Lost)
  {
    if (_secondsSinceStart < m_nextAttemptAt)
    {
      return;
    }
    m_nextAttemptAt = _secondsSinceStart + RECONNECT_INTERVAL_SECONDS;

    const std::string host = m_host;
    const std::uint16_t port = m_port;
    const std::string token = m_token;
    if (Open(host, port, token))
    {
      ++m_reconnects;
    }
    return;
  }

  // ---- Landing --------------------------------------------------------------------------------
  //
  // A connect in flight is asked how it is doing, once a frame, and nothing else happens until it
  // has answered. This is the polling half of the change that stopped `Socket::Connect` blocking.
  if (m_status == Status::Connecting && !Settle(_secondsSinceStart))
  {
    return;
  }

  if (m_status != Status::Greeting && m_status != Status::Playing)
  {
    return;
  }

  // Outgoing first, so an order given this frame is on its way before anything is read. It costs
  // nothing and means a player who clicks and immediately loses their connection still sent it.
  while (!m_outgoing.empty())
  {
    const std::int32_t written = m_socket.Send(m_outgoing);
    if (written < 0)
    {
      m_status = Status::Lost;
      return;
    }
    if (written == 0)
    {
      break;
    }
    m_outgoing.erase(m_outgoing.begin(), m_outgoing.begin() + written);
  }

  std::array<std::uint8_t, 8192> buffer = {};
  for (;;)
  {
    const std::int32_t read = m_socket.Receive(buffer);
    if (read < 0)
    {
      m_status = Status::Lost;
      return;
    }
    if (read == 0)
    {
      return;
    }

    if (!m_incoming.Feed(std::span{buffer.data(), static_cast<std::size_t>(read)}))
    {
      m_status = Status::Lost;
      return;
    }

    std::vector<std::uint8_t> payload;
    while (m_incoming.Take(payload))
    {
      Handle(payload);
      if (m_status != Status::Greeting && m_status != Status::Playing)
      {
        return;
      }
    }
  }
}

} // namespace Lockstep
