#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace Neuron
{

/// Turns a host name into an address without stopping the caller.
///
/// **`getaddrinfo` is the one blocking call left on the client's frame loop** (ADR-043 took the
/// connect off it and said so). It is instant for the dotted address the game offers by default and
/// can sit on a name server for seconds for a real hostname -- and the client draws on the thread
/// that calls it, so those seconds are a frozen window, repeated by every reconnection attempt.
///
/// This is a lookup that has been STARTED and can be asked about, in the same shape as
/// `Socket::Connect` and `Socket::Progress`: begin it, poll it every frame, read the answer when it
/// lands. The thread exists only for the duration of one lookup.
///
/// **A numeric address never gets a thread.** `Start` answers immediately when the host is already
/// an address, which is the case Phase 0 actually uses and the case where spawning a thread to do
/// nothing would be the only cost. `Ready` is then true on the first poll.
class HostLookup
{
public:
  enum class State : std::uint8_t
  {
    /// Nothing has been asked for.
    Idle,
    /// The lookup is running. Ask again next frame.
    Pending,
    /// `Address` holds an address the socket layer can connect to.
    Ready,
    /// The name does not resolve.
    Failed
  };

  HostLookup() = default;
  ~HostLookup();

  HostLookup(const HostLookup&) = delete;
  HostLookup& operator=(const HostLookup&) = delete;
  HostLookup(HostLookup&&) = delete;
  HostLookup& operator=(HostLookup&&) = delete;

  /// Begins a lookup, abandoning any lookup already in flight.
  ///
  /// A host that is already numeric resolves here, on this thread, without one -- `inet_pton` is a
  /// parse and not a query.
  void Start(const std::string& _host);

  /// Where the lookup has got to. Cheap enough to call every frame.
  [[nodiscard]] State Progress() const noexcept;

  /// The resolved address, meaningful only once `Progress` is `Ready`.
  [[nodiscard]] std::string Address() const;

  /// Forgets the lookup. A thread still running is left to finish and detached from the result,
  /// because a lookup cannot be cancelled -- what it must not do is write into a `HostLookup` that
  /// has gone, which is why the shared state outlives both.
  void Reset() noexcept;

private:
  /// What the worker writes and the caller reads. Shared by pointer so that a lookup abandoned
  /// mid-flight has somewhere to land that is not this object.
  struct Answer
  {
    std::atomic<State> state{State::Idle};
    /// Written once, before `state` becomes `Ready`, and read only after. The atomic is the fence.
    std::string address;
  };

  std::shared_ptr<Answer> m_answer;
  std::thread m_worker;
};

} // namespace Neuron
