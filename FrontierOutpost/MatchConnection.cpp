// MatchConnection.cpp -- the client's end of the wire.

#include "pch.h"
#include "MatchConnection.h"

#include <array>

namespace Frontier
{

bool MatchConnection::Open(const std::string& _host, std::uint16_t _port, const std::string& _token)
{
  m_socket = Neuron::Socket::Connect(_host, _port);
  if (!m_socket.Valid())
  {
    return false;
  }

  m_incoming.Reset();
  m_outgoing.clear();
  m_status = Status::Greeting;
  m_refusal = Neuron::RefusalReason::None;

  Send(Neuron::Protocol::EncodeHello(_token));
  return true;
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
    if (Neuron::Protocol::DecodeState(_payload, m_tick, m_secondsToLock, m_snapshot, m_digest))
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

void MatchConnection::Pump()
{
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

} // namespace Frontier
