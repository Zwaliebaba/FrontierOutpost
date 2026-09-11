#pragma once

#include "FrameStream.h"
#include "Protocol.h"
#include "Socket.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

/// The client's end of the wire.
///
/// **The client talks TCP even when the server is in the same process** (ADR-028). That is the
/// point of it: there is no local path that works and a network path that is under-tested, because
/// the host's own client is a socket client like everybody else. Every run of this executable
/// exercises the transport.
///
/// It holds the latest state the server sent and nothing else. No history, no reconciliation, no
/// prediction — at four messages a day there is nothing to predict, and the server is
/// authoritative anyway.
class MatchConnection
{
public:
  enum class Status : std::uint8_t
  {
    /// Nothing has been attempted yet.
    Idle,
    /// Connected, hello sent, waiting to be welcomed.
    Greeting,
    /// Welcomed. Playing.
    Playing,
    /// The server said no. `Refusal()` says why.
    Refused,
    /// The connection is gone.
    Lost
  };

  /// Connects and sends a hello. Returns false if nothing is listening, which for a host-and-play
  /// process means the server thread has not come up yet and the caller should try again.
  [[nodiscard]] bool Open(const std::string& _host, std::uint16_t _port, const std::string& _token);

  /// Reads whatever arrived. Call it every frame; it never blocks.
  void Pump();

  /// Sends an order set, replacing whatever was sent before. Silently does nothing when not
  /// playing — an order given while disconnected is not an error the player can act on, and the
  /// reconnect will send the current rail anyway.
  void SendOrders(std::span<const std::uint8_t> _orderSet);

  /// Says "still here" without submitting anything. Presence is a fact about being seen.
  void SendPing();

  [[nodiscard]] Status State() const noexcept
  {
    return m_status;
  }
  [[nodiscard]] Neuron::RefusalReason Refusal() const noexcept
  {
    return m_refusal;
  }
  [[nodiscard]] std::int32_t Player() const noexcept
  {
    return m_player;
  }
  [[nodiscard]] std::uint32_t Tick() const noexcept
  {
    return m_tick;
  }
  [[nodiscard]] std::int64_t SecondsToLock() const noexcept
  {
    return m_secondsToLock;
  }

  /// The latest snapshot and digest, as bytes. Empty until the first `State` arrives.
  [[nodiscard]] const std::vector<std::uint8_t>& Snapshot() const noexcept
  {
    return m_snapshot;
  }
  [[nodiscard]] const std::vector<std::uint8_t>& Digest() const noexcept
  {
    return m_digest;
  }

  /// True once, after each new state arrives, so the caller knows to rebuild the screen rather
  /// than rebuilding it every frame.
  [[nodiscard]] bool TakeFreshState() noexcept;

private:
  void Send(std::span<const std::uint8_t> _payload);
  void Handle(std::span<const std::uint8_t> _payload);

  Neuron::Socket m_socket;
  Neuron::FrameStream m_incoming;
  std::vector<std::uint8_t> m_outgoing;

  Status m_status = Status::Idle;
  Neuron::RefusalReason m_refusal = Neuron::RefusalReason::None;
  std::int32_t m_player = -1;
  std::uint32_t m_tick = 0;
  std::int64_t m_secondsToLock = 0;

  std::vector<std::uint8_t> m_snapshot;
  std::vector<std::uint8_t> m_digest;
  bool m_fresh = false;
};

} // namespace Frontier
