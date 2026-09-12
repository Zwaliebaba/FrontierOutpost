# ADR-036 — Seats are chosen before the match, and a token is generated per seat

**Status:** Accepted — amended twice by the owner on 2026-09-11. **(1)** The screen runs *after* login, not before: the server opens as a lobby with no match and `ENTER MATCH` creates it, which is the order a player expects and the one that lets the screen show who has arrived. The "What this gives up" section below is therefore spent — the live CONNECTED column exists, by the second route it names. **(2)** Amended by ADR-037: `BOT` works, and so do `BOT TAKES OVER` and `FILL WAITING WITH BOTS` -- the three controls the "What this gives up" section below records as drawn and inert. **(3)** The screen shows **six** seats rather than twelve, so every seat is required (`MINIMUM_PLAYERS`) and `EMPTY` is gone: a seat that cannot be empty needs no button saying it could be. `MAXIMUM_PLAYERS` is still twelve and nothing but the layout assumes six. **(4)** The first open question below, whether a token survives a restart of the host, is closed by ADR-042: the tokens are in the match store and a resumed match admits the same people to the same seats.

**Date:** 2026-09-11
**Decided by:** Owner, on the seats screen mockup, 2026-09-11.

**Supersedes:** the fixed token list of ADR-029. The rest of ADR-029 — a token is a seat and not a
password, a refused token is never logged, nothing is accepted before a `Hello` — stands unchanged.

---

## Context

The owner drew a seats screen: twelve cards, each `EMPTY | HUMAN | BOT`, each carrying a token the
host can copy, with a per-seat rule for what happens if nobody has connected by the first lock, and
`ENTER MATCH` at the bottom.

Two things in it are decisions rather than drawing.

**Where the tokens come from.** ADR-029 pinned six fixed strings — `alpha` … `foxtrot` — in the
composition root, for Phase 0 and explicitly for Phase 0 only. It named the next step itself:
*"Option D is the smallest step — tokens generated per match and shown to the host — and it needs
somewhere for `--serve` to print them."* The seats screen is that somewhere. It also needs more than
six: the screen has twelve seats and `MatchRules` allows twelve.

**When the screen runs.** The mockup shows live `CONNECTED` badges and a running countdown, which
means the match has already started and players are arriving before the first lock. But the number
of seats decides how the galaxy is generated — `playerCount` reaches `GalaxyGenerator`, which places
one capital and one starting cluster per player — so the count has to be settled *before*
`Match::Create` runs. A screen that both chooses the count and shows a running match is asking for
the match to exist before its shape is known.

## Decision

**The seats screen runs before the match starts.** It chooses how many seats there are and issues a
token for each, and `ENTER MATCH` is what creates the galaxy and starts the server. Nothing is live
on it because there is nothing running yet to be live.

**A token is generated per seat**, from a PRNG seeded off the wall clock, in the shape `XXXX-XXXX`
from an alphabet with no `0`/`O` or `1`/`I` in it — because these are read off one screen and typed
into another, usually after being copied into a chat window by hand.

**`playerCount` is the number of seats that are not empty**, and the remaining seats are numbered in
order. Marking a middle seat empty closes the gap rather than leaving a hole: a player index is a
position in the galaxy generator's output, not a label the host chose.

**Fewer than six seats cannot enter.** `MatchRules::Check` refuses it as `PlayerCountOutOfRange`, so
the screen refuses it first, where the reason can be shown.

## What this gives up, and why

**The live `CONNECTED` column is not in this pass.** It is the half of the mockup that says who has
turned up, and it is genuinely useful — the host wants to know whether to wait. It needs the server
running while the screen is still open, which needs the seat count, which is what the screen is
choosing. The way out is one of:

- **Generate for twelve always**, and let a seat nobody takes be unclaimed from tick zero. The
  screen could then run alongside a live server exactly as drawn. It costs a generation rule this
  game does not have — an empty seat's systems start owned by nobody — and it makes every match a
  twelve-player galaxy whatever the turnout.
- **Two phases**: choose the count, start the server, then keep the screen open showing who arrives
  and let the host still flip a seat between human and bot until the first lock. Closer to the
  drawing, and it needs the host's process to read its own server's connection state, which is one
  accessor rather than a protocol message because the host owns the object.

The second is the smaller step and the one to take. It is not in this pass because it is worth
having the screen at all first.

**`BOT` is drawn and inert.** The owner chose the sequence (screen first, bots after) and the
policies still live in the test suite. The toggle refuses rather than pretending: a seat that says
BOT and then plays nothing would be worse than a seat that says it cannot yet.

**"An empty seat's systems start unclaimed" is not implemented.** In this pass an empty seat is a
seat that does not exist, so the galaxy is simply smaller. The mockup's stronger reading — twelve
systems' worth of capitals with some unowned — is the first bullet above.

## Consequences

**The host stops being player zero by accident.** They choose a seat like everybody else, which
finishes what the join screen started (ADR-029's first open question).

**Tokens are not secrets and are now also not guessable by reading the source**, which is a
side-effect rather than the point. Anyone who learns one can still play that seat, exactly as
ADR-029 says, and the seats screen shows all twelve to whoever is sitting at the host's machine.

**A match store written by an earlier build still carries its own tokens**, because the store holds
the rules and the seed and the server is constructed with the token list beside them. Reloading an
old store with new tokens would let somebody play a seat the store does not recognise — nothing does
that today, and whoever adds match resumption from this screen must not.

## Open questions

**Whether a token should survive a restart of the host.** It does not: `ENTER MATCH` generates a new
set, so a host who closes the window has invalidated everything they sent their friends. The match
store is the obvious place to keep them and R13 already permits that file.

**Whether the host should be able to reissue one seat's token mid-match.** The mockup's `NEW TOKEN`
implies yes. Nothing supports it, and it needs the server to accept a token table change.
