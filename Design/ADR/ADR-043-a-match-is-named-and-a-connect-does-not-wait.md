# ADR-043 — A match is named, and a connect does not wait

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, "ok go", 2026-09-12, on the first two items of the codebase review's §4.

**Closes:** `Design/Archive/2026-09-12-codebase-review.md` §4 items 1 and 2.

---

## Context

Two findings from the review, related only in that both are about a process meeting the outside
world.

**Every match wrote to the same two files.** `lockstep-match.store` and `lockstep-match.log`, beside
the executable. Two servers on one machine overwrote each other — and since ADR-042 made a store a
match that resumes, the second server did not merely lose a log, **it ate a match**.

**The client's connect blocked on the thread that draws.** A host that resolves and then never
answers sits in the OS connect timeout: twenty seconds of frozen window on the first attempt, and
because the reconnect loop tries every two seconds, a window that freezes in bursts for as long as
the player leaves it open. ADR-038 had already recorded this as the reason screen 05's CONNECTING
dialog could not cover the part of the wait that matters — nothing pumps, draws, or could read a
`CANCEL` button while the socket is opening.

## Decision

### The files are named after the match

`--store <name>` names them, and the default is derived from the port: `lockstep-7341.store` and
`lockstep-7341.log`. A port is already unique per server on one machine, so the default collides
only between servers that could not both have started.

**The log takes the same stem as the store**, which is why the flag names a stem rather than a file.
A store and the log of the match it holds must not come apart: the log is how anybody works out what
the store contains.

**A store written before this change is not found.** It is called `lockstep-match.store` and nothing
looks for that any more. Resumption itself is a day old, so no real match is affected; a legacy name
would be a permanent path for a one-day-old problem. Rename the file to resume it.

### The connect starts and is polled

`Socket::Connect` makes the socket non-blocking **before** calling `connect`, so the call returns at
once with the handshake in flight, and `Socket::Progress` reports `Pending`, `Ready` or `Failed`.
`select` with a zero timeout answers it; `SO_ERROR` is consulted rather than trusting writability,
because a socket that failed after the select is still reported writable.

**The pending flag moves with the handle.** `MatchConnection` starts a connect and move-assigns the
socket into place, so a move that dropped it would make `Progress` report an unconnected socket
ready and the first `Send` would fail for no visible reason.

**`MatchConnection` gains a `Connecting` status**, and the hello is sent when the peer answers rather
than in `Open` — queuing bytes for a peer that has not agreed to exist yet was only ever harmless
because the connect had already finished by then. A half-open connection is given five seconds
before it is called dead: the OS gives up much later, and what the number really sets is how quickly
the screen stops saying CONNECTING.

**Screen 05's CONNECTING dialog now covers the whole wait**, which is what ADR-038 said it could
not. It also stopped lying about which half it is in: "Reaching the server" until the peer answers,
"Sending token — waiting for Welcome" after.

## What this does not fix

**`getaddrinfo` is still synchronous.** It is instant for the dotted address the game offers by
default and can block on a name server for a real hostname. That one needs a thread rather than a
poll, and a thread is a bigger change than this was.

## Consequences

**The `--join` startup path waits for a settled answer rather than for a return value.** `Open`
succeeding no longer means anything reached the server, so the bounded retry that used to call
`Open` until it stopped failing now re-opens whenever an attempt dies and gives the whole business
two seconds before falling through to the join screen.

**A reconnect passes through `Connecting`**, and the match loop treats that as still-lost. Letting
the CONNECTION LOST dialog blink out for the length of a handshake and back in would read as the
connection returning and going again.

**`MatchServerTests`' client settles its handshake** instead of assuming a loopback connect finishes
inside the call. It usually does; since this change it is allowed not to, and a test that sent into
a half-open socket would have failed on a busy machine and nowhere else.

**Four socket tests**, including the one that is the actual regression: `Connect` to 192.0.2.1
(TEST-NET-1, RFC 5737 — reserved for documentation, routed nowhere) must *return* in under a second.
A network that answers "unreachable" makes it pass trivially; one that drops the packet makes it
pass only because the connect no longer waits.

Verified by running a client at that address: the window draws, the dialog counts its seconds, and
`CANCEL` is there to press.

## Open questions

**Nothing cleans up a store.** A finished match is moved aside to `.finished` (ADR-042) and stays
there; a machine that has hosted twenty matches has forty files beside the executable. Naming them
made that visible rather than causing it — they used to overwrite each other, which is a worse
tidiness.
