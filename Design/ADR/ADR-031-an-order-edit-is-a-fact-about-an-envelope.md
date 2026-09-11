# ADR-031 — An order edit is a fact about an envelope

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Build session for Phase 0 preparation, closing ADR-030's open question.
**Supersedes:** —

---

## Context

The test plan's instrumentation list asks for one timestamped event this tree did not produce:

> *"Timestamped events for: login, session start/end, **order edit**, order lock, message, proposal
> sent/accepted/declined, trade lane opened/cancelled, capital fall, custodian takeover, fleet order
> after capital fall."*

It is not a decoration. H4 is measured off it:

> *"H4 — The 30-minute session exists. Median session 10–40 min; **≥ 80% of sessions include an
> order edit**."*

ADR-030 shipped everything else on that list and left this one open, with a sentence that turns out
to be wrong in an instructive way: *"the server sees a replacement order set arrive, and the game
sees only the last one before the lock, so today the count of edits is a fact only the client
holds."*

The second half is right. `MatchSimulation::Submit` overwrites the pending slate, so by the lock
there is one order set per player and no trace of how many there were. The first half is where the
mistake is: the server *does* see each arrival, and counting arrivals requires reading none of them.

## Options considered

### A. The client counts and reports its own edits

The client knows exactly what the player did. It also has every reason to be wrong: a client that
reports its own instrumentation is a client whose bug becomes a finding, and reconnection means the
count has to survive a socket that just died. It also puts a message in the protocol that exists
only to be logged.

### B. The game counts, and narrates it like everything else

Consistent with ADR-030 — the game narrates, the server writes it down. But an order edit is not a
thing that happens in the world. Nothing in the galaxy changes when somebody moves a fleet marker
twice before the lock; the resolution is identical either way. To count it, `MatchSimulation` would
have to keep state that exists only to be logged, and that state would sit inside the class whose
hash defines whether two builds agree.

### C. The server counts envelopes

`MatchServer` already routes every `Orders` message. How many arrived for a player since the last
lock is a property of the messages, knowable without opening one, and the class that knows it is the
one that already refuses to look inside (ADR-025).

## Decision

**C. The server counts envelopes, and an edit is the second or later order set for the same tick.**

The first set of a tick is a turn being given. Every one after it replaces what was there, which is
what a player experiences as changing their mind, and the lock is what makes the next one a turn
again.

**Two counters, because H4 asks two different questions:**

**Whether a submission replaced one is a fact about the player's turn**, so it is counted per player
and survives them reconnecting mid-tick. Somebody who submits, drops their connection and comes back
to submit again has edited their turn, however many sockets carried it.

**Whether a session contained an edit is a fact about the connection**, so it is counted per
connection. A session *is* a connection here: somebody who plays, closes their lid and comes back
has had two of them, and H4 is a fraction of sessions.

**Both land in the log, in two shapes.** `order-edit player=0 this-tick=2` at the moment it happens,
because the plan asks for a *timestamped* event; and `player 0 disconnected orders=5 edits=3` on the
line that ends the session, so that "what share of sessions included an edit" is one column of one
row per session rather than a join across forty-eight hours that somebody does by hand and does
differently each time.

## Consequences

**No count reaches the simulation.** This is the property that matters: a number that varies with
how somebody's network behaved, inside the class whose hash decides whether a reload reproduced,
would be a determinism bug wearing an instrumentation costume (R16). The counters live in
`MatchServer`, which the store never sees.

**A malformed order set is counted as an edit.** The server does not know it is malformed and must
not look. `MatchSimulation::RejectedSubmissions` is where that shows instead, and a Phase 0 with a
non-zero count there has a client bug to find before it has an H4 answer.

**An edit is a submission, not a change.** A client that sent an identical order set twice would be
counted as having edited. This tree's client only sends on a tap that changed something, so the two
coincide today — but they are not the same thing, and a future client that re-sent on reconnect
would inflate H4 without anybody noticing. Whoever changes `MatchConnection` to re-send should read
this paragraph.

**Sessions that never end have no summary line.** The server writes the totals when it notices a
disconnect, which it does on the next failed read. A process killed mid-match loses its last
session's line, and the per-edit lines it had already written survive.

## What this changes elsewhere

`MatchServer` gains two counters per connection and one vector per match, and one more line shape in
a log that already had several. Nothing crosses the seam; `Simulation` is untouched.

## Open questions

**Whether the time between edits is worth having.** H4's other half is session *length*, which is
derivable from the login and disconnect lines because `MatchLog` stamps both. How long somebody sat
with a turn open before changing it is not, and might be the more interesting number.

**Whether a tap that changes nothing should count.** It cannot reach here — the client sends only
when its state changed — so the question is really whether that client behaviour is load-bearing for
a measurement, and it is.
