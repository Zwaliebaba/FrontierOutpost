# ADR-006 — The transport's queues: a mutex and a ring, fixed capacity, and two different overflow policies

**Status:** Accepted

**Date:** 2026-09-09
**Decided by:** Build session MVP-01, step 5.
**Supersedes:** —

---

## Context

Threading is already settled: the server has its own thread from the first commit, the client
thread never blocks on the server and the server never touches D3D12 (MVP-01 §2). What is left is
the queue between them — how it is built, how big it is, and what it does when it is full.

The traffic is small and known. The server pushes one 32-byte state per tick, 20 a second. The
client pushes one order per click, so a few a second at most and only while someone is clicking.
Contention is a few dozen lock acquisitions a second between exactly two threads.

## Options considered

### A. A lock-free single-producer/single-consumer ring

The textbook answer for exactly this shape, and genuinely free of the lock. Two atomic indices,
acquire/release ordering, no kernel transition.

At this traffic it wins nothing measurable: the contended case essentially never happens, so the
mutex is an uncontended atomic exchange either way. What it costs is real — a hand-written
lock-free ring is the kind of code that is correct on x86's memory model and subtly wrong on a
weaker one, and it is wrong in a way that shows up as a message that vanished once, months later,
on somebody else's machine. It is also harder to give a bounded-capacity drop policy, which is the
half of this decision that actually matters.

### B. A `std::mutex` and a `std::deque`

The obvious answer. It allocates, and it grows without bound: a queue nobody is draining is a
queue that consumes memory until something else fails. For a server that is a denial of service
with extra steps.

### C. A `std::mutex` and a fixed-capacity ring

A mutex, an inline `std::array`, and a head and count. Nothing allocates after construction and
the memory is bounded by construction.

## Decision

**C.** `Neuron::MessageQueue<T, Capacity, Policy>` is a `std::mutex` and an inline ring buffer.

The lock is chosen deliberately, not by default: at 20 Hz there is nothing to win from A, and the
interface here is exactly what a lock-free implementation would have to satisfy on the day
profiling says otherwise. Bounded capacity is the property that matters more than the locking, and
it is what B gives up.

**The two directions have different overflow policies, and this is the substance of the ADR.**

- **Orders drop the *newest*.** An order is something a player meant, and they are a sequence: the
  ship goes to the last place clicked. Dropping from the front would apply an old click and
  discard the current one, which is the wrong click. Refusing the new one at least leaves the
  queue coherent, and the sender learns about it from the return value.
- **States drop the *oldest*.** A state is a fact that is about to be superseded. A client that
  has fallen behind wants the newest position; the one it missed is of no use to anybody, and
  ADR-005's client interpolates from whatever pair it has.

`Push` returns whether **this** message was stored, which is false in exactly one case: a full
queue under drop-newest. That is narrower than it first looks and the narrowness was arrived at by
getting it wrong — an earlier version returned "has this queue ever dropped anything", and a
caller that retries on false then retries forever on a message it had already stored. The
concurrency test in `NeuronCoreTests` hung on precisely that.

`DroppedCount()` counts the times the queue was full and never resets, because a fault counter
that goes back to zero hides the fault.

**Capacities: 256 orders, 64 states.** Both are sized for the failure they bound rather than for
the traffic. 256 orders is thirteen seconds of a person clicking as fast as anyone can; 64 states
is three seconds of a client that has stopped drawing. Neither will ever fill in this game. The
MMO will fill both, and when it does the behavior is defined rather than discovered.

## Consequences

**What this makes easy.** The failure mode is bounded and visible. A queue cannot consume memory,
cannot block the thread on the other side of it, and says how many messages it has refused.

**What this makes hard.** Nothing yet. The lock will eventually show up in a profile of a server
with thousands of connections, and at that point the answer is A behind the same interface.

**What it costs.** A mutex acquisition per message. Measured at nothing here; asserted rather than
measured, because at 20 Hz there is no measurement that would be meaningful.

**What it forecloses.** A caller cannot block until there is room. Nothing wants to — blocking is
what MVP-01 §2 rules out — but it means a producer that must not lose a message has to retry, and
the concurrency test does exactly that.

## What this changes elsewhere

- **Code:** `NeuronCore/MessageQueue.h` and `NeuronCore/LoopbackTransport.{h,cpp}`.
  `NeuronCoreTests` exercises both policies and runs a producer and a consumer on separate threads
  through 20,000 messages with a capacity of 256, so the wrap-around and the full-queue path are
  both covered rather than just the happy path.
- **AGENTS.md:** no change. R15 allows standard containers by default, and an inline array is not
  an allocator.
- **Design/:** no document superseded.

## Open questions

What a `UdpTransport` does about ordering and loss, which is the thing that makes the loopback's
guarantees look generous. `LoopbackTransport` delivers in order and never loses a message that was
accepted; UDP does neither, and ADR-005 already notes that the client trusts what it is given.

Whether the state queue should be a single slot rather than 64. If the client only ever wants the
newest state, a queue is arguably the wrong shape and a mailbox is the right one. Left as a queue
because the client currently drains it to find the *pair* it interpolates between, and a mailbox
would give it only the newest.
