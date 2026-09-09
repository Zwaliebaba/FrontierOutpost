#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace Neuron
{

/// What a full queue does to the message that will not fit (ADR-006).
enum class OverflowPolicy : std::uint8_t
{
  /// Throw away the oldest. Right for state: a client that has fallen behind wants the newest
  /// position, and the one it missed is of no use to anybody.
  DropOldest,
  /// Throw away the newest. Right for orders: they are a sequence a player meant, and dropping
  /// from the front would apply their most recent click and discard the one before it.
  DropNewest
};

/// A fixed-capacity queue between two threads.
///
/// One mutex and a ring buffer, and not a lock-free structure. That is a decision with a reason
/// rather than a first draft (ADR-006): at 20 Hz this is contended a few dozen times a second, so
/// there is nothing to win, and a hand-written lock-free ring is the kind of code that is subtly
/// wrong on one memory model and fine on another. When profiling says the lock is the problem,
/// the interface here is what a lock-free implementation would have to satisfy.
///
/// The capacity is fixed and the storage is inline, so nothing allocates after construction and a
/// queue cannot grow without bound while nobody is draining it. What it does instead is drop, by
/// Policy, and count what it dropped -- because a queue that silently loses messages and a queue
/// that loses messages and says so are very different things to debug.
template <typename T, std::size_t Capacity, OverflowPolicy Policy> class MessageQueue
{
public:
  static_assert(Capacity > 0, "A queue that cannot hold anything is not a queue.");

  /// True when this message was stored.
  ///
  /// It is false in exactly one case: a full queue under DropNewest, where the message handed in
  /// is the one thrown away. Under DropOldest a full queue still stores it -- it made room first
  /// -- and DroppedCount() is how the caller finds out something was lost.
  ///
  /// The return value is about THIS message and nothing else. Reporting a queue that has ever
  /// dropped anything as permanently failing would be worse than useless: a caller that retries
  /// on false would then retry forever on a message it had already stored.
  bool Push(const T& _message)
  {
    const std::scoped_lock lock{m_mutex};

    if (m_count == Capacity)
    {
      ++m_droppedCount;

      if constexpr (Policy == OverflowPolicy::DropNewest)
      {
        return false;
      }
      else
      {
        m_head = (m_head + 1) % Capacity;
        --m_count;
      }
    }

    m_storage[(m_head + m_count) % Capacity] = _message;
    ++m_count;
    return true;
  }

  /// Takes the oldest message, if there is one.
  [[nodiscard]] bool Pop(T& _outMessage)
  {
    const std::scoped_lock lock{m_mutex};

    if (m_count == 0)
    {
      return false;
    }

    _outMessage = m_storage[m_head];
    m_head = (m_head + 1) % Capacity;
    --m_count;
    return true;
  }

  /// How many messages this queue has refused or discarded over its life -- under DropNewest the
  /// ones it refused, under DropOldest the ones it overwrote. A caller that retries a refusal has
  /// lost nothing, so this is a count of times the queue was full rather than of messages the
  /// system lost.
  ///
  /// Never resets: it is a fault counter, and a fault counter that goes back to zero hides the
  /// fault.
  [[nodiscard]] std::uint64_t DroppedCount() const
  {
    const std::scoped_lock lock{m_mutex};
    return m_droppedCount;
  }

  [[nodiscard]] std::size_t Count() const
  {
    const std::scoped_lock lock{m_mutex};
    return m_count;
  }

private:
  mutable std::mutex m_mutex;
  std::array<T, Capacity> m_storage = {};
  std::size_t m_head = 0;
  std::size_t m_count = 0;
  std::uint64_t m_droppedCount = 0;
};

} // namespace Neuron
