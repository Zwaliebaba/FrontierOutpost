#pragma once

#include "Session.h"

#include "FrameStream.h"
#include "Protocol.h"
#include "Socket.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Neuron
{

/// One match, on a socket.
///
/// Owns a `Session`, a listener and a connection per player. `Poll(now)` does everything: accept,
/// read, route, resolve if a lock is due, push the new state. **It is called from one thread and
/// touches nothing another thread can see** — which is what lets the executable run a server and a
/// client in the same process without a lock anywhere near the simulation (ADR-028).
///
/// It never decodes an order set. Orders arrive as bytes, go to the session as bytes, and reach the
/// game at the far side of the seam (ADR-025). What this class understands is *envelopes*: who is
/// connected, whether they said hello, and whether a frame is well formed.
class MatchServer
{
public:
  /// Takes the session and listens on `_port`. Pass zero to be given a free one, which is what a
  /// test wants and what a host who does not care would use.
  MatchServer(std::unique_ptr<Session> _session, std::uint16_t _port, std::vector<std::string> _tokens);

  /// Listens with NO MATCH YET. This is the lobby: people connect, present a token, are given a
  /// seat, and wait.
  ///
  /// **A server has to be able to exist before a match does**, because the order a player expects
  /// is log in, then start a game -- and because "how many are playing" cannot be answered until
  /// they have arrived, while the galaxy cannot be generated until it is answered. Before `Begin`
  /// this server accepts a `Hello`, refuses everything that would change a match, and pushes no
  /// state, because there is none.
  MatchServer(std::uint16_t _port, std::vector<std::string> _tokens);

  /// Installs the match. Everything that was waiting starts playing at the next poll.
  void Begin(std::unique_ptr<Session> _session);

  [[nodiscard]] bool Started() const noexcept
  {
    return m_session != nullptr;
  }

  /// Which seats have a welcomed connection on them, one entry per token. What a lobby screen
  /// draws, and what "you cannot start until the humans are here" is decided from.
  [[nodiscard]] std::vector<bool> SeatsConnected() const;

  [[nodiscard]] bool Listening() const noexcept
  {
    return m_listener.Valid();
  }

  /// The port actually bound. The only way to learn it when the caller asked for zero.
  [[nodiscard]] std::uint16_t Port() const
  {
    return m_listener.Port();
  }

  [[nodiscard]] const Session& Match() const noexcept
  {
    return *m_session;
  }

  /// Accept, read, route, resolve, push. Returns how many ticks resolved.
  std::uint32_t Poll(Instant _now);

  /// How many players are connected and have been welcomed.
  [[nodiscard]] std::int32_t Connected() const;

  /// Every event worth logging, oldest first, cleared as it is taken.
  ///
  /// The test plan's Phase 0 asks for timestamped events -- login above all, because the login
  /// curve is its primary instrument -- and a server that cannot produce them answers nothing.
  /// They are taken rather than pushed so the caller decides where they go.
  [[nodiscard]] std::vector<std::string> TakeLog();

private:
  /// One connected client.
  struct Connection
  {
    Socket socket;
    FrameStream incoming;
    std::vector<std::uint8_t> outgoing;
    /// Which player, once they have said hello. Invalid before that, and a connection that has not
    /// said hello may send nothing else.
    std::int32_t player = -1;
    bool closing = false;

    /// How many order sets this connection sent, and how many of them replaced one it had already
    /// sent for the same tick. **This is H4's measurement** -- "at least 80% of sessions include an
    /// order edit" -- and it is per connection rather than per player because a session is a
    /// connection: somebody who plays, closes their lid and comes back has had two of them.
    std::uint32_t ordersSent = 0;
    std::uint32_t editsMade = 0;

    /// Whether this connection has already been logged for ordering at a match that has ended.
    /// One line per session: a player tapping at a finished game is one fact, not twenty.
    bool orderedAfterTheEnd = false;
  };

  void Accept();
  void Read(Connection& _connection);
  void Handle(Connection& _connection, std::span<const std::uint8_t> _payload);
  void Flush(Connection& _connection);
  void Send(Connection& _connection, std::span<const std::uint8_t> _payload);
  void PushState(Connection& _connection, Instant _now);
  void Log(std::string _line);

  [[nodiscard]] std::int32_t PlayerFor(const std::string& _token) const;

  std::unique_ptr<Session> m_session;
  Socket m_listener;
  /// Player n plays on token n. A list rather than a map, because the order is the player order and
  /// a map would be one more thing to iterate in a defined order.
  std::vector<std::string> m_tokens;
  std::vector<Connection> m_connections;
  std::vector<std::string> m_log;

  /// Order sets received per player since the last lock, cleared by it.
  ///
  /// Per player rather than per connection, because *whether a submission replaced one* is a fact
  /// about the player's turn and survives them reconnecting mid-tick, while *whether a session
  /// contained an edit* is a fact about the connection. They are different questions and H4 asks
  /// the second one off the first.
  ///
  /// **It never reaches the simulation.** A count that varied with how somebody's network behaved
  /// would be a count that could change a resolution, and the hash with it (R16).
  std::vector<std::uint32_t> m_submissionsThisTick;
  std::uint32_t m_pushedTick = 0;
  bool m_pushedOnce = false;
};

} // namespace Neuron
