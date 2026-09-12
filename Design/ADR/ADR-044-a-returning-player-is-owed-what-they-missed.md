# ADR-044 — A returning player is owed what they missed, and the server can be heard

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, "do 5, 11 and the console", 2026-09-12.

**Closes:** the codebase review's §4 item 11, and the half of its §2 *"the dedicated server was not
operable as built"* that remained. ADR-028's open question, restated by ADR-042, is answered.

---

## Context

**The server kept one digest.** A player who closed a lid overnight reconnected, was told how many
ticks they had missed — `SINCE YOU LOOKED · T43 > T46`, `3 TICKS` — and was shown the events of only
the last of them. The header and the four-cell delta were honest about the span; the list under them
was not, and there was nothing else to show. SCREENS.md 01 asks for "digest from `TickLog` +
previous unread ticks", and there were no previous unread ticks to have.

**The dedicated server was mute.** `--serve` is a Windows-subsystem binary, so it has no console
unless it asks for one, and every line it produced went through `DebugTrace` — `OutputDebugStringA`
in Debug and **`__noop` in Release**. A person running the server the way a server is run saw
nothing at all: not the port, not a login, not the reason it stopped. The match log had it, which is
no help to somebody watching a window to see whether their friends have arrived.

## Decision

### The session keeps eight ticks of digest

Per player, oldest first, captured as each tick resolves — a digest is made from the `TickLog` of the
tick that just ran, and there is no way back to an earlier one once the next has replaced it.

**Eight, which is two days at the authored four locks a day.** Not unbounded: a three-week match is
eighty-four ticks and a server that kept every digest for twelve players would be keeping the match
twice, once as orders in the store and once as prose. A player away longer is shown the eight most
recent and told how many ticks they missed — a worse answer than all of them, and a much better one
than a number.

**A `State` carries a list of digests, each with its tick.** A digest with no tick on it cannot be
filed against what the player last read. The count is bounded on the wire by a constant and checked
before it is used to reserve anything: a peer that says four billion digests is a peer, not a
server, and that is the allocation it would like.

**The whole backlog on arrival, the newest tick on every tick after.** A client that has been
watching already has the rest, and re-sending them four times a day to twelve people would be
re-sending a match nobody missed.

**The client concatenates the ones newer than what it last drew**, oldest first, and `ViewOf` builds
one digest out of them. `drawnTick` is the composition root's memory and R13 leaves the client
nothing to write, so a restarted client has read nothing and takes the lot — which is honest rather
than wrong: it has not looked at any of this.

### The server speaks

A console for `--serve`: attached to the launching shell's when there is one, because that is where
the person who typed the command is looking; a fresh one when there is not.

**A redirected handle is honored before a console is asked for.** `--serve > today.txt` is how
anybody would keep a day of it, and a process that attached a console instead would write to a
window and leave the file empty. A Windows-subsystem process inherits its parent's standard handles
exactly as a console one does; what it does not get is a console of its own.

Written to the handle rather than through the CRT's `stdout`, because a GUI-subsystem process starts
with the CRT's streams pointing at nothing. Flushed, because a server that crashes must not take its
last words with it. It opens by saying its port, its store and its log.

## Consequences

**`Protocol::EncodeState` changed shape**, so every caller and both fuzz tests moved with it. The
truncation test still walks every cut of the message and the noise test still feeds it garbage;
both now do it to a message with a list in it, which is the shape that has a count a peer could lie
about.

**`TestClient` grew `FindLast`.** `Find` returns the first message of a kind, and for a client that
has been watching several ticks the first `State` is the one it was welcomed with — sent before
anything had resolved, and so the one message guaranteed to say nothing about the match. That was a
test failing for the right reason and asserting against the wrong message.

**Screen 08 is now buildable.** Its tabs per unread tick were waiting on exactly this, and
`Design/UI/SCREENS.md` says so. It is not built here: this pass gives it the data.

## Open questions

**The backlog is bounded and silent about it.** A player away for three days gets eight ticks and no
mark saying the rest is gone. The tick count in the header is right, the list is short, and nothing
reconciles the two. Screen 08's tabs are where that belongs.

**Nothing re-sends a digest that was dropped in flight.** The backlog is sent once, on the state
that follows a `Hello`. A client that received that state and then failed to decode it keeps the
last state it could trust (ADR-042's rule) and does not ask again — it will simply be one tick
behind until it reconnects.
