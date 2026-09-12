#pragma once

#include "FrameStream.h"
#include "Protocol.h"
#include "Socket.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace Lockstep
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

  /// Connects again with the host, port and token this connection already has.
  ///
  /// **`RETRY` on a refusal needs this and `RetryNow` will not do.** A refusal is deliberately
  /// final -- `Pump` stops pumping a refused connection, because an unknown token will still be
  /// unknown in three seconds -- so the only way back from one is to open a new connection. SEAT IN
  /// USE is the refusal this is for: it stops being true the moment the other link drops.
  [[nodiscard]] bool Reopen()
  {
    const std::string host = m_host;
    const std::uint16_t port = m_port;
    const std::string token = m_token;
    Reset();
    return Open(host, port, token);
  }

  /// Tries the next reconnection now rather than at the end of the interval. `RETRY NOW` on
  /// screen 04 is this, and it is the whole of what that button can honestly promise: the loop was
  /// going to try anyway, and this is the player saying they would rather not wait for it.
  void RetryNow() noexcept
  {
    m_nextAttemptAt = 0.0;
  }

  /// Puts this back to `Idle`: socket closed, refusal forgotten, nothing being retried.
  ///
  /// **What `BACK` on a refusal dialog does.** Without it a refused connection stays refused for
  /// the life of the process, because a refusal is deliberately final -- which is right while the
  /// dialog is up and wrong the moment the player has edited the token and wants to try again.
  void Reset() noexcept;

  /// Reads whatever arrived, and reconnects if the connection has gone. Call it every frame; it
  /// never blocks.
  ///
  /// **Reconnection is not optional for Phase 0.** Six people over forty-eight hours will close a
  /// lid, lose a wifi connection or walk through a lift, and a client that gave up on the first
  /// dropped packet would end their match. The server already sends a snapshot on `Hello`, so
  /// coming back is a reconnect and nothing more -- there is no state on this side to reconcile.
  ///
  /// `_secondsSinceStart` is the caller's clock, used only to space the attempts.
  void Pump(double _secondsSinceStart);

  /// Sends an order set, replacing whatever was sent before.
  ///
  /// **Silently does nothing when not playing, and nothing re-sends it afterwards.** There is no
  /// code on either side of the socket that replays an order given while the link was down. Screen
  /// 04 says so rather than promising otherwise (`ConnectionDialog`); replaying it is an open
  /// question in ADR-038.
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

  /// How many times this connection has come back. Shown on the screen, because a player whose
  /// orders are not arriving needs to know that rather than wonder.
  [[nodiscard]] std::uint32_t Reconnects() const noexcept
  {
    return m_reconnects;
  }

  /// Whether the client is currently able to reach the server.
  [[nodiscard]] bool Live() const noexcept
  {
    return m_status == Status::Playing;
  }

  /// How long until the next reconnection attempt, from the same clock `Pump` is given. Zero when
  /// nothing is being retried.
  ///
  /// Screen 04 counts this down: a dialog that says "reconnecting" and nothing else is
  /// indistinguishable from a dialog that has given up.
  [[nodiscard]] double SecondsToNextAttempt(double _secondsSinceStart) const noexcept
  {
    return m_status == Status::Lost ? std::max(0.0, m_nextAttemptAt - _secondsSinceStart) : 0.0;
  }

  /// Where this connection points, for a dialog that has to name it.
  [[nodiscard]] std::string Server() const
  {
    return m_port == 0 ? m_host : std::format("{}:{}", m_host, m_port);
  }

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

  /// Kept so a reconnect needs nothing from the caller.
  std::string m_host;
  std::uint16_t m_port = 0;
  std::string m_token;

  double m_nextAttemptAt = 0.0;
  std::uint32_t m_reconnects = 0;
};

} // namespace Lockstep
