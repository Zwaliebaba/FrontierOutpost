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

Socket::Socket(Socket&& _other) noexcept
  : m_handle(_other.m_handle)
{
  _other.m_handle = NOTHING;
}

Socket& Socket::operator=(Socket&& _other) noexcept
{
  if (this != &_other)
  {
    Close();
    m_handle = _other.m_handle;
    _other.m_handle = NOTHING;
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
    Shutdown();
  }
}

Socket Socket::Listen(std::uint16_t _port)
{
  Startup();

  const SOCKET handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (handle == INVALID_SOCKET)
  {
    Shutdown();
    return {};
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

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(_port);

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
  hints.ai_family = AF_INET;
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
  for (const addrinfo* candidate = found; candidate != nullptr; candidate = candidate->ai_next)
  {
    handle = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (handle == INVALID_SOCKET)
    {
      continue;
    }

    // Connected while still blocking, so a refusal is reported here rather than surfacing later as
    // a socket that never produces anything. Everything after this point is non-blocking.
    if (connect(handle, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) != SOCKET_ERROR)
    {
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

  MakeNonBlocking(handle);
  return Socket{static_cast<std::uintptr_t>(handle)};
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

  sockaddr_in address = {};
  int length = sizeof(address);
  if (getsockname(static_cast<SOCKET>(m_handle), reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR)
  {
    return 0;
  }
  return ntohs(address.sin_port);
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
