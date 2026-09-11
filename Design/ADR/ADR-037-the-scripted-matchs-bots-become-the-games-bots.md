# ADR-037 — The scripted match's bots become the game's bots

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Owner, "proceed with the bots", 2026-09-11.

**Amends:** ADR-036's "`BOT` is drawn and inert". The toggle works. Everything else in ADR-036
stands.

---

## Context

ADR-036 shipped a seats screen with a `BOT` button that refused, and said why: the policies lived in
`Tests/GameLogicTests/ScriptedMatchTests.cpp`, and a seat that said BOT and then played nothing
would be a worse lie than a seat that says it cannot yet.

Those policies were written for step 9 of `Design/Plans/4X-01-CoreLoop.md` — six bots playing a
whole match, to prove the rules worked together before anybody played them. They already had to be
good enough to *reach* every mechanic: an earlier version looked one lane ahead, and all six empires
sat on two systems each for eighty ticks and never met, which is a harness failure that looks
exactly like a rules failure. The replacement walks the lanes it knows about breadth-first.

So the question was not "how should a bot play" but "where should the bot that already plays live".

## Decision

**The scripted match's policies move into `GameLogic/BotPolicy`, and the test file drives the
shipped ones.** The suite's whole-match run is now evidence about the code a host puts in a seat,
rather than about a second implementation that agreed with it on the day it was written. Writing a
separate "real" bot would have cost the suite that.

`BotOrdersFor(policy, snapshot, rules)` is the whole interface. Six policies exist; the seats screen
offers **three** — CAUTIOUS (`Turtle`), STEADY (`ExpandNear`), AGGRESSIVE (`Raider`). The other
three are the scripted match's: one that spreads itself thin on purpose, one that only ever offers
trade lanes, and one that does nothing at all so the custodian rules have somebody to fire at.
None is an opponent a person would choose.

**A bot plays from the snapshot and never from the match.** It is handed the same fogged view a
person in that seat would be sent. This is not politeness — it is the reason the policies were
usable in the first place, because a policy that cannot find something it needs to decide is a
snapshot that is missing something a real client would also be missing.

**`MatchSimulation::PlayBots` runs just before the lock**, inside `GameLogic`, where R16's
determinism rules apply. A bot's orders are encoded and put in the same pending slot a person's
would occupy, so they are locked into `LockedTurn()` beside them and the match store records a
bot's tick exactly as it records a person's — which means a replay of that store does not need the
bots at all.

**Whatever arrived first is played.** If a submission is already in a bot seat's slot, the bot does
not touch it.

**A bot is never absent.** The custodian rules exist for a person who stopped turning up, and a seat
that plays itself cannot be that.

**The roster goes in `Configuration()`**, appended after the seed, one byte per seat. A store
reloaded without it would have live seats nobody plays: they would go absent, the custodian rules
would fire, and the reloaded match would diverge from the recorded one for a reason nothing wrote
down. `0xFF` means a person's seat, and a configuration that simply ends at the seed — one written
before bots existed — reloads as the all-human match it recorded.

## What this fixed on the way past

`MatchSimulation::FromConfiguration` fed the stored seed back into `Match::Create`, and
**`Match::Create` searches from its seed rather than using it**: it walks the seed through the
mixer, and keeps walking until the generator accepts one, so the seed a match reports through
`Seed()` is the accepted one and not the one it was asked for. Reloading through `Create` therefore
started a fresh search from an already-final seed and built a **different galaxy**, silently, which
is the one thing a match store exists to prevent (ADR-024).

Nothing had ever reloaded a store, so nothing had ever noticed. The bots' round-trip test is what
found it. `Match::Reload` and `GalaxyGenerator::Regenerate` are the entry points that do not search;
fresh matches are unaffected, because `Create` still does exactly what it did.

## The seats screen's dead controls come alive

Three things ADR-036 drew and refused now do something, and they are worth naming because each was
recorded as a decision with nowhere to go:

- **`BOT`** on a card. Refused on the host's own seat, and on a seat somebody has already connected
  to — the token is theirs until they drop.
- **`BOT TAKES OVER`** in the detail panel, which was "recorded, and it needs bots". It is now what
  makes `ENTER MATCH` reachable when a friend does not show: a seat marked this way counts as ready
  while still empty, and *entering* is what converts it. Up to that moment the host can still
  change their mind and wait. `GOES CUSTODIAN` stays the default, because the host usually does
  want to wait.
- **`FILL WAITING WITH BOTS`** in the footer, which was `FILL EMPTY WITH BOTS` and refused. Every
  seat still waiting, except the host's and anyone already connected.

## Consequences

**A one-person match is possible.** The host takes a seat, fills the other five, and enters. That
was not reachable before today: `MINIMUM_PLAYERS` is six and every seat needed a person behind it.

**A match's result now depends on who was a machine**, so `bot-seat player=N style=…` goes into the
instrumentation log beside `match-start` (ADR-030). A Phase 0 number drawn from a match where three
empires were played by the machine means something different, and nothing else records which.

**`Snapshot::System` exists**, because the bots and the scripted match were each carrying their own
copy of the same lookup. Second caller, so it became a layer (R2).

## Open questions

**A bot cannot take over a seat mid-match.** The screen's language says "IF STILL WAITING AT T1
LOCK" and the conversion happens at `ENTER MATCH`, which is before T1 rather than at it. A player
who connects and then disappears at tick 40 still goes to a custodian. Closing the gap needs the
server to change a running simulation's roster, which nothing supports.

**The host's style choices are not offered per-difficulty.** Three styles is not three difficulties —
AGGRESSIVE is not "harder", it is a different game. Whether a player wants difficulty instead is a
question for whoever plays six bots for an evening.
