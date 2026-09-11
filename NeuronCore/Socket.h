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

  /// Connects to a host. Returns an invalid socket if the name does not resolve or nothing is
  /// listening. Blocking for the duration of the connect and only that -- a connect that cannot be
  /// waited for is a connect whose failure nobody can report.
  [[nodiscard]] static Socket Connect(const std::string& _host, std::uint16_t _port);

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

  /// `INVALID_SOCKET`, spelled without needing WinSock2 in this header.
  static constexpr std::uintptr_t NOTHING = static_cast<std::uintptr_t>(~0ULL);

  std::uintptr_t m_handle = NOTHING;
};

} // namespace Neuron
