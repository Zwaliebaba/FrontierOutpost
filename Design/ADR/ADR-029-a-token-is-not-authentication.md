# ADR-029 — A token is a seat, not authentication

**Status:** Accepted — of its open questions, on 2026-09-12: the host's seat is the token their client presented (ADR-036, ADR-041); tokens are generated per match and kept in its store, so they live exactly as long as the match (ADR-036, ADR-042); a connection that drops and returns mid-tick is still refused as `AlreadyConnected` until the server notices, and the SEAT IN USE dialog's RETRY is what the screen offers for it (ADR-038).

**Date:** 2026-09-11
**Decided by:** Build session for `Design/Plans/4X-02-ServerAndClient.md`, Step 3.
**Supersedes:** —

---

## Context

Six people need to connect to a match and each be the right player. That is the entire requirement
for Phase 0, and the test plan is explicit about what it is for: *"Timestamped events for: login,
session start/end... The login curve is the primary instrument; surveys are secondary."*

So the thing that has to exist is not security. It is **an identity stable enough to log**. A Phase
0 that cannot draw the login curve answers nothing, and one where two people accidentally play the
same empire answers worse than nothing.

The pressure is to build more than that. Accounts, passwords, a lobby — all obviously needed one
day, all obviously not needed by six friends playing a forty-eight-hour match. `4X-02` §4 names this
as one of the things that will tempt you, and the answer there is *"It needs a token and a login
log. Everything else is v2."*

## Options considered

### A. Nothing — first connection gets player 0, second gets player 1

Simplest, and it fails the first time somebody's client reconnects after a dropped connection: they
come back as whoever is next in line, which is somebody else's empire. It also makes the login log
meaningless, because a login cannot be attributed to a person.

### B. A per-match, per-player token, issued by whoever starts the server

Six strings. Each names a seat. The host tells each player theirs, and they type it once.

Not authentication in any sense: the tokens are in the binary, they cross the wire in the clear, and
anybody who learns one can play that seat. What they do provide is exactly what Phase 0 needs —
a stable identity per person, so a reconnect returns to the same empire and the log can say who
logged in when.

### C. Accounts, hashed passwords, a registration flow

The real answer for a public game and a large amount of work that Phase 0 would not use. It also
needs storage that outlives a match, which is a second persistence decision after ADR-024 and one
nobody has a requirement for yet.

### D. Per-match tokens generated at startup and printed

B, with the tokens random rather than fixed. Strictly better for anything real, and it needs a way
for the host to read them and distribute them — a file, a console, a screen. `--serve` has no
window.

## Decision

**B, for Phase 0 only, and the ADR says so in its title so that nobody has to read the body to learn
it.**

Six fixed tokens, one per seat, in player order. A `Hello` carries one; the server maps it to a
player index and welcomes them. An unknown token is refused with `UnknownToken`; a token already in
use is refused with `AlreadyConnected`, which is what makes a reconnect return to the same empire
rather than take a new one.

**Three properties that are not about security and are worth stating:**

**A refused token is never written to the log.** It is the only secret in the protocol and a log
file is the likeliest place for a secret to end up somewhere it should not. The log says *"refused
an unknown token"* and nothing more. There is a test for this, because it is the kind of line
somebody helpfully improves.

**A successful login is logged, with the player index.** That is the test plan's primary instrument
and the reason any of this exists.

**Nothing is accepted before a Hello.** An unidentified connection that could submit orders would be
one that could play somebody else's turn — which is the actual threat model for six friends: not an
attacker, a mistake.

## Consequences

**Anybody who learns a token can play that seat**, and the tokens are in the binary, so anybody with
the executable has all six. For six people who know each other on a known host this is fine, and it
would be indefensible for anything else.

**A Phase 1 with strangers needs a different answer.** Option D is the smallest step — tokens
generated per match and shown to the host — and it needs somewhere for `--serve` to print them.
Beyond that is option C and a real decision about storage.

**The login curve is drawable.** `MatchServer::TakeLog` produces the events and the caller decides
where they go, which for now is the debug output.

**Losing a token loses a seat.** There is no recovery flow because there is nothing to recover
from: the host knows all six.

## What this changes elsewhere

`Protocol` gains `Hello`, `Welcome` and `Refused`; `MatchServer` holds the token list and the
mapping. The six strings live in `Lockstep.cpp`, in the composition root, because who is
allowed to play is a property of the match being hosted rather than of the server library.

## Open questions

**Whether the host should pick which seat they are.** They are player 0 by taking the first token,
which is arbitrary and fine, and would stop being fine the moment seat choice mattered.

**What happens when a player's connection drops and returns mid-tick.** They are refused as
`AlreadyConnected` until the server notices the old connection has gone, which it does on the next
failed read. That is usually immediate and is not guaranteed to be.

**Whether a token should expire with the match.** It has no lifetime at all today; a match store
reloaded next week accepts the same six tokens.
