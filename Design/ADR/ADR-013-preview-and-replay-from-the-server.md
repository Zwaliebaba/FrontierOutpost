# ADR-013 — The engagement preview and the combat replay come from the server; the client holds no rules

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10.
**Supersedes:** —

---

## Context

The one-pager promises that "the client shows a preview of any engagement from visible
information", and that combat is deterministic with fixed rounds and integer arithmetic. The
client cannot compute a preview without the combat rules, and `AGENTS.md` §2 says a client-side
file never reaches `GameLogic` — the day one does is the day the server stopped being
authoritative. ADR-015 also wants to replay a resolved engagement as a scene, which needs the
round-by-round result.

The game is asynchronous. A round trip to the server takes a fraction of a second and the player
is composing orders for a tick hours away.

## Options considered

### A. A shared rules library both halves link

Move the combat arithmetic out of `GameLogic` into a library the client links too. Preview works
with no server. The rule in `AGENTS.md` §2 is rewritten, the seam of ADR-007 weakens, and the
client now has a copy of the rules that has to be kept identical to the server's — the divergence
the whole architecture is built to make impossible.

### B. The server computes on request

`Simulation::Preview(seat, request)` runs the resolver's combat phase on the seat's visible state
with the seat's pending orders applied and every other seat's orders unchanged, and returns the
round log. The client draws it. The digest already carries the round log of every engagement the
seat could see (ADR-005), so the replay is the same record.

It costs a request and a response, and a preview that is only as current as the last snapshot —
which is exactly what "from visible information" means.

## Decision

**B.** The client never computes an outcome. `Preview` is a request of ADR-006's protocol; the
server answers with an `EngagementLog` — per round, each fleet's strength before and after, and
the survivors — computed by the same combat functions `ResolveTick()` runs (ADR-004), on a copy of
the visible state. A `Digest` carries an `EngagementLog` for every engagement the seat saw. The
client draws both through one path (ADR-015) and does not know which it is drawing.

**The preview uses visible information only, by construction:** the input is the seat's
`VisibleSnapshot`, not the match state. A preview cannot leak a hidden order because the function
that computes it was never given one.

## Consequences

**What this makes easy.** The client has no rules and the seam holds. Preview and replay are one
record and one renderer. The preview is testable in `GameLogicTests` as a function of a visible
snapshot.

**What this makes hard.** No preview offline. A player composing orders on a train sees the map and
their book, and the preview arrives when the connection does.

**What it costs.** A round trip per preview, and one more combat entry point that has to agree with
the resolver — which it does by being the same function.

**What it forecloses.** Client-side what-if over hidden state, which the design forbids anyway.

## What this changes elsewhere

- **AGENTS.md:** no change. §2's rule is what this preserves.
- **Design/:** ADR-005 (the digest carries the log), ADR-007 (`Preview` on the seam), ADR-015
  (the replay). `Design/Plans/MVP-02-TheLoop.md` steps 2, 3 and 6.
- **Code:** nothing yet.

## Open questions

Whether the preview should also answer "what if I add this order" for orders not yet submitted.
The plan treats the pending book as the what-if: edit, preview, edit again. A speculative order
that is not in the book is a later request type if the test plan shows it is wanted.
