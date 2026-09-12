// Socket.cpp -- the only file in this tree that knows what WSAGetLastError is.

#include "pch.h"
#include "Socket.h"

#include <atomic>

namespace Neuron
{

namespace
{

/// How many things are using WinSock. `WSAStartup` and `WSACleanup` are reference counted by the
/// OS as well, but a count here means `Startup` can be called from anywhere without anybody
/// tracking whether somebody already did.
std::atomic<std::int32_t> g_users{0};

[[nodiscard]] bool WouldBlock() noexcept
{
  return WSAGetLastError() == WSAEWOULDBLOCK;
}

void MakeNonBlocking(SOCKET _socket) noexcept
{
  u_long yes = 1;
  (void)ioctlsocket(_socket, FIONBIO, &yes);
}

} // namespace

void Socket::Startup()
{
  if (g_users.fetch_add(1) == 0)
  {
    WSADATA data = {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
      Fatal("WinSock would not start.");
    }
  }
}

void Socket::Shutdown() noexcept
{
  if (g_users.fetch_sub(1) == 1)
  {
    (void)WSACleanup();
  }
}

Socket::Socket(std::uintptr_t _handle) noexcept
  : m_handle(_handle)
{
}

Socket::~Socket()
{
  Close();
}

// A half-finished connect moves with its handle. `MatchConnection` starts one and move-assigns it
// into place, so a move that dropped the flag would make `Progress` report an unconnected socket
// ready and the first `Send` would fail for no visible reason.
Socket::Socket(Socket&& _other) noexcept
  : m_connecting(_other.m_connecting),
    m_handle(_other.m_handle)
{
  _other.m_handle = NOTHING;
  _other.m_connecting = false;
}

Socket& Socket::operator=(Socket&& _other) noexcept
{
  if (this != &_other)
  {
    Close();
    m_connecting = _other.m_connecting;
    m_handle = _other.m_handle;
    _other.m_handle = NOTHING;
    _other.m_connecting = false;
  }
  return *this;
}

bool Socket::Valid() const noexcept
{
  return m_handle != NOTHING;
}

void Socket::Close() noexcept
{
  if (m_handle != NOTHING)
  {
    (void)closesocket(static_cast<SOCKET>(m_handle));
    m_handle = NOTHING;
    m_connecting = false;
    Shutdown();
  }
}

Socket Socket::Listen(std::uint16_t _port)
{
  Startup();

  // **IPv6, with v4 mapped into it** (ADR-046). One socket serves both families: Windows defaults
  // `IPV6_V6ONLY` to on, so it is turned off here and a v4 peer arrives as `::ffff:a.b.c.d`. The
  // alternative is two listeners and two accept loops for a game whose players are as likely to be
  // on one as the other.
  const SOCKET handle = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (handle == INVALID_SOCKET)
  {
    Shutdown();
    return {};
  }

  DWORD v6Only = 0;
  if (setsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6Only), sizeof(v6Only)) == SOCKET_ERROR)
  {
    // A host with IPv4 only. Nothing to do about it here and nothing to say: `bind` below decides
    // whether this socket can serve anybody, and it is the honest place for that to fail.
    v6Only = 1;
  }

  // SO_EXCLUSIVEADDRUSE, not SO_REUSEADDR. On Windows the reuse option is not the POSIX one: it
  // lets a second socket bind the port this one is listening on, after which which of the two gets a
  // connection is undefined -- Microsoft's own guidance is that a server setting it "must be
  // considered to be not secure". The exclusive option refuses that bind. Its cost is documented
  // too: a listener cannot be rebound while a connection it accepted is still draining, so a server
  // restarted seconds after an abrupt exit can fail to bind until the old connections are gone. That
  // failure is reported by the caller rather than papered over, and the answer is to start again.
  BOOL exclusive = TRUE;
  (void)setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

  sockaddr_in6 address = {};
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_any;
  address.sin6_port = htons(_port);

  if (bind(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
      listen(handle, SOMAXCONN) == SOCKET_ERROR)
  {
    (void)closesocket(handle);
    Shutdown();
    return {};
  }

  MakeNonBlocking(handle);
  return Socket{static_cast<std::uintptr_t>(handle)};
}

Socket Socket::Connect(const std::string& _host, std::uint16_t _port)
{
  Startup();

  addrinfo hints = {};
  // Whatever the name has. `getaddrinfo` returns the families in the order the system prefers and
  // the loop below tries them in turn, so a host with both gets whichever answers.
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* found = nullptr;
  const std::string port = std::to_string(_port);
  if (getaddrinfo(_host.c_str(), port.c_str(), &hints, &found) != 0 || found == nullptr)
  {
    Shutdown();
    return {};
  }

  SOCKET handle = INVALID_SOCKET;
  bool pending = false;
  for (const addrinfo* candidate = found; candidate != nullptr; candidate = candidate->ai_next)
  {
    handle = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (handle == INVALID_SOCKET)
    {
      continue;
    }

    // Non-blocking BEFORE the connect, which is the whole change: `connect` returns immediately
    // with WSAEWOULDBLOCK and the handshake carries on underneath. `Progress` is where it lands.
    MakeNonBlocking(handle);

    if (connect(handle, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) != SOCKET_ERROR)
    {
      // A loopback peer can answer inside the call. Nothing to wait for.
      break;
    }

    if (WSAGetLastError() == WSAEWOULDBLOCK)
    {
      pending = true;
      break;
    }

    (void)closesocket(handle);
    handle = INVALID_SOCKET;
  }

  freeaddrinfo(found);

  if (handle == INVALID_SOCKET)
  {
    Shutdown();
    return {};
  }

  Socket started{static_cast<std::uintptr_t>(handle)};
  started.m_connecting = pending;
  return started;
}

Socket::Connection Socket::Progress()
{
  if (!Valid())
  {
    return Connection::Failed;
  }
  if (!m_connecting)
  {
    return Connection::Ready;
  }

  const SOCKET handle = static_cast<SOCKET>(m_handle);

  // Writable means the handshake finished; the exception set is how Windows reports one that was
  // refused. Both are asked at once, with no wait, because this is called from a frame.
  fd_set writable = {};
  fd_set failed = {};
  FD_ZERO(&writable);
  FD_ZERO(&failed);
  FD_SET(handle, &writable);
  FD_SET(handle, &failed);

  timeval immediately = {};
  const int ready = select(0, nullptr, &writable, &failed, &immediately);
  if (ready <= 0)
  {
    return ready == 0 ? Connection::Pending : Connection::Failed;
  }

  if (FD_ISSET(handle, &failed))
  {
    return Connection::Failed;
  }

  // Writable is not the same as connected: a socket that failed after the select can still be
  // reported writable, and SO_ERROR is the authority on which it was.
  int problem = 0;
  int size = static_cast<int>(sizeof(problem));
  if (getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&problem), &size) == SOCKET_ERROR || problem != 0)
  {
    return Connection::Failed;
  }

  m_connecting = false;
  return Connection::Ready;
}

Socket Socket::Accept()
{
  if (!Valid())
  {
    return {};
  }

  const SOCKET accepted = accept(static_cast<SOCKET>(m_handle), nullptr, nullptr);
  if (accepted == INVALID_SOCKET)
  {
    return {};
  }

  // Each accepted socket holds its own WinSock reference, because each one outlives the listener
  // in principle and closes independently.
  Startup();
  MakeNonBlocking(accepted);
  return Socket{static_cast<std::uintptr_t>(accepted)};
}

std::uint16_t Socket::Port() const
{
  if (!Valid())
  {
    return 0;
  }

  // Big enough for either family, and the family is read out of what was written rather than
  // assumed: a listener is v6 and a socket that came from `Connect` is whichever the name resolved
  // to. The port sits at a different offset in each.
  sockaddr_storage address = {};
  int length = sizeof(address);
  if (getsockname(static_cast<SOCKET>(m_handle), reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR)
  {
    return 0;
  }

  if (address.ss_family == AF_INET6)
  {
    return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
  }
  if (address.ss_family == AF_INET)
  {
    return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
  }
  return 0;
}

std::int32_t Socket::Receive(std::span<std::uint8_t> _into)
{
  if (!Valid() || _into.empty())
  {
    return 0;
  }

  const int read = recv(static_cast<SOCKET>(m_handle), reinterpret_cast<char*>(_into.data()), static_cast<int>(_into.size()), 0);
  if (read > 0)
  {
    return read;
  }

  // Zero from recv means the peer closed cleanly, which is NOT the same as nothing to read -- that
  // is WSAEWOULDBLOCK. Conflating them is how a server keeps a dead connection forever.
  if (read == 0)
  {
    return -1;
  }
  return WouldBlock() ? 0 : -1;
}

std::int32_t Socket::Send(std::span<const std::uint8_t> _bytes)
{
  if (!Valid() || _bytes.empty())
  {
    return 0;
  }

  const int written = send(static_cast<SOCKET>(m_handle), reinterpret_cast<const char*>(_bytes.data()), static_cast<int>(_bytes.size()), 0);
  if (written >= 0)
  {
    return written;
  }
  return WouldBlock() ? 0 : -1;
}

} // namespace Neuron
