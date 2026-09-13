# ADR-071 — A hostname resolves off the frame loop, and a numeric one never leaves it

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Build session for `Design/Plans/4X-03-PhaseZero.md`, Step 6.
**Supersedes:** — <!-- it finishes what ADR-043 named and left -->

---

## Context

ADR-043 took the connect off the client's frame loop: `Socket::Connect` starts a non-blocking
connect and `Progress` is polled once a frame, because the client draws on the thread that called
it and a wrong host used to freeze the window for the OS timeout — twenty seconds on the first
attempt and, since the reconnect loop tries every two seconds, in bursts for as long as the player
left it open.

It named the half it did not fix, in `Socket.h` and again in `4X-02` §7: **`getaddrinfo` is still
synchronous.** It is instant for the dotted address the game offers by default and can sit on a
name server for seconds for a real hostname. Six people typing `192.168.1.20` are unaffected; six
people typing `lockstep.example.com` are not, and every reconnection attempt pays it again.

This is the last item on `Design/Plans/4X-03-PhaseZero.md`, and the plan says plainly that it is the
one the owner may drop. It is here because a hosted server — which Phase 1 needs and `blueprint.md`
§7 names — is reached by a name, not by a dotted quad.

## Options considered

### A. `GetAddrInfoExW` with an overlapped completion

The Win32 answer: an asynchronous resolve with a completion the frame loop can poll. No thread of
our own. It is more Win32 surface than anything else in `NeuronCore` — an `OVERLAPPED`, a handle to
cancel, a completion routine — and it is Windows-only in a file whose job is to be the only one that
knows what `WSAGetLastError` is. The tree already crosses a thread boundary in `HostedServer` and
does not yet own an overlapped anything.

### B. The query on a worker thread, polled like the connect

A small type that starts a thread, runs `getaddrinfo` on it, and reports `Pending` / `Ready` /
`Failed` — the same shape as `Socket::Connect` and `Socket::Progress`, which is the shape the client
already polls. Portable, and the one new idea is a thread the caller never joins.

The cost is a thread per lookup, and the ownership question of what happens to it when the player
closes the window mid-query.

### C. Leave it

Defensible: a Phase 0 host hands out a dotted address, and nobody hits it. It leaves a known
freeze in the client for whenever Phase 1's hosted server arrives, and leaves `Socket.h` carrying a
comment about a defect rather than a rule.

## Decision

**B**, with two rules that matter more than the mechanism.

**A numeric host never starts a thread.** `HostLookup::Start` tries `inet_pton` for both families
first — a parse, never a query — and answers `Ready` on the spot when the host is already an
address. The case Phase 0 actually uses costs one parse and no thread, and is `Ready` on the first
poll, so nothing about the common path got slower to fix the rare one.

**An abandoned lookup is detached, not joined.** A name query cannot be cancelled, and a player
closing a window must not wait out a name server to do it. The worker writes into an `Answer` held
by `shared_ptr`, so the state it writes into outlives the `HostLookup` that started it; destroying
or resetting one is immediate. The string is written before the atomic `state`, and the state is
what the caller polls, so the release/acquire pair on that one atomic is what makes the string safe
to read across the thread.

`MatchConnection` gains a `Resolving` status, driven by the poll that already drives `Connecting`,
and connects to the **address the lookup returned** rather than to the name — so `Socket::Connect`'s
own `getaddrinfo` is a parse and the name is queried exactly once. The connect deadline covers both,
because a player waiting on a name server is not waiting on something different from a player
waiting on a handshake. Screen 05's CONNECTING dialog covers all three parts of getting in.

## Consequences

A hostname no longer freezes the window: measured at under a millisecond per poll while a query is
in flight (`PollingALookupDoesNotStopTheCaller`), against a query that can take seconds. Tearing
down a lookup mid-query is under 100 ms (`AnAbandonedLookupDoesNotHoldTheCallerUp`), which is the
window-close path.

**A detached thread can outlive the process's interest in it**, which is the honest cost of B. It
holds a `shared_ptr` to its own answer and touches nothing else, so what it can leak is one thread
and one string for the length of one DNS timeout. A process exiting while one is in flight is the
usual Windows teardown of a detached thread, and nothing in it touches the client's state.

`NeuronCore` now owns a thread, which it did not before. It is confined to this one type.

The client's status enum grows a state that the dialog does not distinguish, which is deliberate:
the player is told the client is getting in, and the three ways it can be doing that are not their
problem.

## What this changes elsewhere

- **Code**: `NeuronCore/HostLookup.{h,cpp}` (new), registered in the project and its filters;
  `LockstepClient/MatchConnection.{h,cpp}` (`Resolving`, the poll, resetting the lookup);
  `Lockstep/Lockstep.cpp` (the new status maps to the CONNECTING dialog and to the reconnect
  overlay). `Socket.h`'s comment about the unfixed half is now false and is corrected.
- **Tests**: `NeuronCoreTests` gains `HostLookupTests` — the numeric path, the failure path, the
  cadence measurement and the teardown measurement.
- **Design/**: `4X-02` §7 item 3 is struck through; `UI/SCREENS.md` 05 notes that the dialog covers
  the lookup too.

## Open questions

**Whether the dialog should say `RESOLVING`** rather than folding the lookup into CONNECTING. The
plan that asked for this suggested it should; the decision above says the player does not need the
distinction, and a third line of dialog copy for a state that is usually over within a frame is
copy nobody reads. If Phase 1's hosted server makes lookups slow enough to notice, the state is
already there to draw.

**Whether a lookup should be cached** for the length of a session. The reconnect loop re-resolves
every attempt, which is correct for a server that has moved and wasteful for one that has not.
Nobody has measured it mattering.
