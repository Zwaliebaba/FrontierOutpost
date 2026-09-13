// HostLookup.cpp -- the blocking name query, moved off the caller's thread.

#include "pch.h"
#include "HostLookup.h"

#include "Socket.h"

#include <array>
#include <utility>

namespace Neuron
{

namespace
{

/// Whether the host is already an address, in either family.
///
/// `inet_pton` PARSES; it never queries a name server, so this is the test that decides whether a
/// thread is needed at all. The dotted address a Phase 0 host hands out lands here and goes no
/// further.
[[nodiscard]] bool IsNumeric(const std::string& _host) noexcept
{
  std::array<std::uint8_t, 16> scratch = {};
  return InetPtonA(AF_INET, _host.c_str(), scratch.data()) == 1 || InetPtonA(AF_INET6, _host.c_str(), scratch.data()) == 1;
}

/// The first address a name resolves to, as text, or an empty string.
///
/// Text rather than a `sockaddr`, because the answer crosses a thread and then goes back into
/// `Socket::Connect`, which takes a host string. A numeric string re-parsed by `getaddrinfo` is a
/// parse and not a second query, so nothing blocks the second time.
[[nodiscard]] std::string FirstAddressOf(const std::string& _host)
{
  Socket::Startup();

  addrinfo hints = {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* found = nullptr;
  if (getaddrinfo(_host.c_str(), nullptr, &hints, &found) != 0 || found == nullptr)
  {
    Socket::Shutdown();
    return {};
  }

  std::string address;
  std::array<char, 64> text = {};
  for (const addrinfo* candidate = found; candidate != nullptr && address.empty(); candidate = candidate->ai_next)
  {
    const void* source = nullptr;
    if (candidate->ai_family == AF_INET)
    {
      source = &reinterpret_cast<const sockaddr_in*>(candidate->ai_addr)->sin_addr;
    }
    else if (candidate->ai_family == AF_INET6)
    {
      source = &reinterpret_cast<const sockaddr_in6*>(candidate->ai_addr)->sin6_addr;
    }

    if (source != nullptr && InetNtopA(candidate->ai_family, source, text.data(), text.size()) != nullptr)
    {
      address = text.data();
    }
  }

  freeaddrinfo(found);
  Socket::Shutdown();
  return address;
}

} // namespace

HostLookup::~HostLookup()
{
  // Detached rather than joined: a name query cannot be cancelled, and a client closing its window
  // must not wait out a name server to do it. The worker writes into the shared answer, which
  // outlives this object precisely so that it has somewhere safe to land.
  if (m_worker.joinable())
  {
    m_worker.detach();
  }
}

void HostLookup::Start(const std::string& _host)
{
  Reset();
  m_answer = std::make_shared<Answer>();

  if (_host.empty())
  {
    m_answer->state.store(State::Failed);
    return;
  }

  // Already an address: answered here, with no thread and no query.
  if (IsNumeric(_host))
  {
    m_answer->address = _host;
    m_answer->state.store(State::Ready);
    return;
  }

  m_answer->state.store(State::Pending);
  m_worker = std::thread(
    [answer = m_answer, host = _host]()
    {
      std::string address = FirstAddressOf(host);

      // The string is written BEFORE the state, and the state is what the caller polls: the release
      // on this store pairs with the acquire on that load, which is what makes the string safe to
      // read on the other thread.
      const bool resolved = !address.empty();
      answer->address = std::move(address);
      answer->state.store(resolved ? State::Ready : State::Failed);
    });
}

HostLookup::State HostLookup::Progress() const noexcept
{
  return m_answer == nullptr ? State::Idle : m_answer->state.load();
}

std::string HostLookup::Address() const
{
  return m_answer == nullptr ? std::string{} : m_answer->address;
}

void HostLookup::Reset() noexcept
{
  if (m_worker.joinable())
  {
    m_worker.detach();
  }
  m_answer.reset();
}

} // namespace Neuron
