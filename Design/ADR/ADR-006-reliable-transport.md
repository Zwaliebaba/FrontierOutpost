# ADR-006 — The transport is reliable and ordered, every request is answered, and the queue stays a thread primitive

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10. Replaces the transport-queues ADR that carried this number (owner decision: the pre-4X record is rewritten in place, `Design/README.md` §4).
**Supersedes:** —

---

## Context

The tree's transport is two fixed-capacity queues with deliberately different overflow policies:
orders drop the newest because "the ship goes to the last place clicked", states drop the oldest
because a state is about to be superseded (`NeuronCore/MessageQueue.h`,
`NeuronCore/LoopbackTransport.{h,cpp}`). Both arguments were right for a 20 Hz ship and both are
wrong for the 4X. An order is now an editable commitment that the player must be able to read
back and that the server may refuse with a reason — a silently dropped edit is a bet the player
thinks they placed. A digest is the primary screen — a dropped one is a missed tick. Nothing that
crosses this transport is disposable.

The other change is that there is now a network. ADR-011 puts the server in its own process, so
the transport that matters is a socket, and the loopback is the development harness's stand-in
for it. `NeuronCore.h` already includes Winsock and links `ws2_32.lib`; nothing uses them yet.
R14 forbids a third-party dependency, so whatever is written is written against the Windows SDK.

## Options considered

### A. Keep the two-queue shape and its policies

The MVP's transport, with the records widened. It cannot answer a request, cannot refuse an order
with a reason, and its overflow policies discard exactly the messages the 4X cannot lose.

### B. Reliable, ordered, framed messages over TCP, with a response to every request

Each message is a length-prefixed frame with a type and a version, serialized field by field
little-endian as `Protocol.cpp` already does. The client sends requests — join, set order,
withdraw order, read order book, read snapshot, read digest, preview — and the server answers each
one, accepted or rejected with a reason code. The server also pushes the tick's snapshot and
digest unrequested. TCP supplies ordering, delivery and flow control, all three of which the
design needs and none of which it is worth writing.

It costs a framing layer, a message vocabulary, and a socket implementation against Winsock. It
also costs head-of-line blocking, which at a few messages an hour is not a cost.

### C. UDP with a reliability layer of our own

The conventional choice for a real-time game, where the latency of a retransmit matters. Here the
fastest thing that happens is a tap, and the response can take a second without anyone noticing.
Everything the layer would add is what TCP already does.

## Decision

**B.** A message is a 4-byte length, a 2-byte message type, a 2-byte protocol version and a
payload serialized field by field, little-endian, by the `Write`/`Read` cursor helpers in
`NeuronCore/Protocol.cpp`. The message types are fixed in `NeuronCore/Protocol.h` and their
records are R8 aggregates. Sizes of fixed-layout records are still asserted at compile time;
lists carry a count.

**Every request has exactly one response**, carrying the request's sequence number and a
`Verdict` — accepted, or rejected with a reason code the client can show. An order that the server
did not accept is not in the order book, and the client learns that from the response rather than
from the next digest.

**`TcpTransport` in `NeuronCore`** is the socket, written against Winsock and nothing else. The
`Transport` concept it and `LoopbackTransport` share is named now, because there are two of them
(R2). `LoopbackTransport` carries the same frames through `MessageQueue` for the in-process
harness of ADR-011.

**`MessageQueue` stays, as what it is: a mutex and a ring between two threads.** Its overflow
policies are no longer a design statement about the game; in the loopback both directions are
sized so that a full queue is a fault, and a full queue asserts rather than drops. `DroppedCount()`
stays as the fault counter it always was.

## Consequences

**What this makes easy.** A refused order is visible. A reconnecting client asks and is answered.
A message that cannot be parsed is a versioned frame that says so, rather than a struct that
deserializes into garbage. The harness and the network run the same bytes.

**What this makes hard.** The protocol is now a vocabulary that has to be kept in step on both
sides, and the version field is what makes a mismatch a message rather than a mystery.

**What it costs.** A socket layer and a framing layer that do not exist today, and the loss of
the loopback's pleasant property that a message is a struct.

**What it forecloses.** Nothing the 4X wants. A real-time mode would want C, and would be a
different game.

## What this changes elsewhere

- **AGENTS.md:** no rule changes. R14 is satisfied by Winsock.
- **Design/:** ADR-005 says what the snapshot and digest carry; ADR-011 says which process is on
  each end. `Design/Plans/MVP-02-TheLoop.md` step 3 is the protocol, step 4 the socket.
- **Code:** nothing yet. `Neuron::MoveToOrder` and `Neuron::ShipState` are deleted by the plan;
  until then `MessageQueue.h`'s and `LoopbackTransport.h`'s citations of "ADR-006" refer to a
  decision this file no longer records.

## Open questions

Whether the server also needs an HTTP face for the digest — a push notification's deep link,
say. Not for the loop; the test plan's phases run on the client alone.

TLS. A per-seat token (ADR-016) crossing a plain TCP connection is readable on the path. Accepted
for Phases 0 and 1 among friends and recruited strangers; a decision before anything is open to
the public, and Schannel is in the SDK.
