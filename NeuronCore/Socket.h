#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace Neuron
{

/// A TCP socket, non-blocking, owning its handle.
///
/// Thin on purpose. It exists so that nothing above it says `WSAGetLastError`, and so that a socket
/// closes when it goes out of scope rather than when somebody remembers — a leaked handle in a
/// process that runs for three weeks is a process that stops accepting connections on day nine.
///
/// **Non-blocking throughout**, because the client's socket is polled from the same loop that draws
/// the screen at sixty frames a second and must never wait on a peer. `Receive` returning zero
/// means *nothing right now*, which is the normal case and not an error.
///
/// Windows only, like the rest of this tree. `NeuronCore.h` already includes WinSock2 and links
/// `ws2_32`, so this costs no new dependency (R14).
class Socket
{
public:
  Socket() = default;
  ~Socket();

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&& _other) noexcept;
  Socket& operator=(Socket&& _other) noexcept;

  /// A listener on `_port`, or an invalid socket. Pass zero to be given a free port, which is what
  /// a test wants and what a host offering "any port" would use.
  [[nodiscard]] static Socket Listen(std::uint16_t _port);

  /// How a connection started by `Connect` is getting on.
  enum class Connection : std::uint8_t
  {
    /// Still reaching for the peer. Ask again next frame.
    Pending,
    /// The peer answered. The socket can be sent on.
    Ready,
    /// Refused, unreachable, or the peer never answered.
    Failed
  };

  /// STARTS connecting to a host, and does not wait for it. Returns an invalid socket only when the
  /// name does not resolve; everything after that is reported by `Progress`.
  ///
  /// **It used to block, and the client draws on the thread that called it.** A host that is merely
  /// wrong resolves and then never answers, so the connect sat in the OS timeout -- twenty seconds
  /// of a frozen window on the first attempt, and, because the reconnect loop tries every two
  /// seconds, a window that froze in bursts for as long as the player left it open. The comment
  /// here used to argue that a connect which cannot be waited for is a connect whose failure nobody
  /// can report; that is only true of a caller with nowhere to put the answer, and this one has a
  /// dialog for it (screen 05).
  ///
  /// **`getaddrinfo` is still synchronous**, which is instant for the dotted address the game
  /// offers by default and can block on a name server for a real hostname. That one needs a thread
  /// rather than a poll, and is not fixed here.
  [[nodiscard]] static Socket Connect(const std::string& _host, std::uint16_t _port);

  /// Where the connection begun by `Connect` has got to. Cheap enough to call every frame, and
  /// meaningless on a socket that came from `Listen` or `Accept` -- those are ready when they
  /// exist, and this says so.
  [[nodiscard]] Connection Progress();

  /// The next pending connection, or an invalid socket if there is none right now.
  [[nodiscard]] Socket Accept();

  [[nodiscard]] bool Valid() const noexcept;

  /// The port this is bound to. Meaningful for a listener, and the only way to learn the port when
  /// `Listen(0)` chose one.
  [[nodiscard]] std::uint16_t Port() const;

  /// Bytes read, `0` for nothing available, `-1` for a peer that has gone or a socket that has
  /// failed. Zero and minus one are genuinely different and callers must not conflate them: one is
  /// a quiet moment and the other is a player who has closed their laptop.
  [[nodiscard]] std::int32_t Receive(std::span<std::uint8_t> _into);

  /// Bytes written, which may be fewer than offered when the send buffer is full. `-1` if the peer
  /// has gone. A caller with more to say keeps the remainder and offers it again.
  [[nodiscard]] std::int32_t Send(std::span<const std::uint8_t> _bytes);

  void Close() noexcept;

  /// Brings WinSock up, once per process, and takes it down when the last user is gone. Called by
  /// `Listen` and `Connect`, so nothing above this file has to think about it.
  static void Startup();
  static void Shutdown() noexcept;

private:
  explicit Socket(std::uintptr_t _handle) noexcept;

  /// Whether this socket is still finishing a connect. False for a listener, for an accepted
  /// peer, and for a connection that has already been reported `Ready` or `Failed`.
  bool m_connecting = false;

  /// `INVALID_SOCKET`, spelled without needing WinSock2 in this header.
  static constexpr std::uintptr_t NOTHING = static_cast<std::uintptr_t>(~0ULL);

  std::uintptr_t m_handle = NOTHING;
};

} // namespace Neuron
