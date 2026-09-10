# ADR-018 — A fleet is a divisible strength, and a garrison is a fleet that cannot leave

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10, answering two things the one-pager assumes and never states.
**Supersedes:** —

---

## Context

Two gaps in the one-pager meet in the same type.

The first is splitting. The second of the three most interesting decisions is *where the fleet is*,
and its argument is that "ships can defend or push, never both; lanes make every allocation a
visible commitment that others can read and exploit". An indivisible fleet has no allocation to
make: it is in one place, and the decision is a move rather than a split. So splitting is not a
convenience the design forgot, it is the mechanism decision two is about. The one-pager never
mentions it.

The second is the garrison. The custodian rule says "their garrisons weaken with each tick of
absence, so the territory is a public race among every neighbour who can reach it". That is the
only appearance of the word in the document. Everything else about combat is written in terms of
fleets: an *incumbent fleet* gets the defender bonus, a claim requires presence "uncontested by any
surviving hostile fleet", each fleet's damage is spread across enemies in proportion to strength.

## Options considered

### A. A fleet is an indivisible unit; a garrison is a defence value on the system

The fleet is a token that moves. A system carries an integer defence that combat folds in as a
defender term, and the custodian rule decrements it.

Combat then has two kinds of participant. The proportional damage split has to decide what share
a system-defence term absorbs; the defender bonus has to apply to two different things; claims and
sieges have to ask whether a defence value counts as "presence"; and the one-pager's sentences
about fleets stop being literally true. And decision two does not exist.

### B. A fleet is a divisible integer strength; a garrison is a fleet pinned to its system

A fleet has an owner, a location (a system, or a lane with departure and arrival ticks) and an
integer strength. `Split` makes two fleets out of one; two fleets of one owner at one system
coalesce. A garrison is an ordinary fleet with a flag saying it will not accept a move order.

One entity, one combat path, and every sentence the one-pager writes about fleets is literally the
code. The cost is that "garrison" is a flag rather than a concept, and that a pinned fleet is a
thing a player can see but not command, which has to be legible on screen.

### C. No garrisons at all

Systems are defended only by fleets left there deliberately. Simplest, and the custodian rule has
nothing to decay, so the argument the design makes for why a dropout is not a private farm has to
be rewritten.

## Decision

**B.**

**A fleet is `{ id, owner, location, strength, pinned }`** with an integer strength. Strength is
the single combat quantity: it is what production buys, what combat reduces in proportion, and what
reaching zero means (a fleet at zero strength ceases to exist; a *seat* whose mobile fleets all
reach zero is Gone, per the one-pager, and pinned fleets do not count towards that).

**Splitting and merging are orders.** `SplitFleet(fleetId, strengthToDetach)` creates a new fleet
of that strength at the same location, refused if the detachment is not strictly between zero and
the fleet's strength. Merging is not an order: **two fleets of the same owner at the same system at
the end of a tick coalesce into the lower id**, which is what stops a player accumulating a hundred
one-strength fleets and what makes the map readable. Fleets in transit never merge, so a lane can
carry several of one owner's fleets with different ETAs.

**A garrison is a pinned fleet.** It is created by the generator at a capital and by a building
that produces one, it takes no move order, it is the incumbent for the defender bonus like any
other fleet, and it counts as presence for claims and sieges like any other fleet. The custodian
rule decrements the strength of a seat's pinned fleets once per tick of absence.

**Nothing else has a strength.** A system is not a combatant.

## Consequences

**What this makes easy.** Decision two exists: a player with one fleet of strength ten can send six
and keep four, and the split is visible to anyone who can see the lane. Combat is one function over
one list. Every claim, siege and capture rule reads exactly as the one-pager writes it. The
custodian decay is one line.

**What this makes hard.** A pinned fleet has to be obviously uncommandable on screen, or the first
thing every player does is try to move their capital's garrison and conclude the game is broken.
ADR-015's scene draws it differently and the orders screen omits it.

**What it costs.** Coalescing is a rule players must learn: strength left at a system merges into
whatever is already there and loses its separate identity. That is the right default and it is a
line in the digest the first time it happens to somebody.

**What it forecloses.** Fleets as named, persistent, individually-tracked units with histories.
This game's fleet is an amount of force in a place.

## What this changes elsewhere

- **AGENTS.md:** no rule changes. The integer remainder rule R16 already carries is what the
  proportional damage split uses.
- **Design/:** ADR-004 (the resolver's phases operate on this), ADR-015 (a pinned fleet is drawn
  and not offered). `Design/Plans/MVP-02-TheLoop.md` slices 1, 3, 4 and 7.
- **Code:** nothing yet.

## Open questions

Whether coalescing should be at the end of the tick, as decided, or on arrival before combat.
End of tick means two of your fleets arriving at a contested system fight as two fleets in that
tick's melee and merge afterwards, which is slightly worse for the arriving player than merging
first. Decided at end of tick because merging before combat would let arrival order inside a phase
change an outcome, which ADR-004 forbids.

What a pinned fleet does when its system is captured. The plan destroys it, since a garrison that
changes hands would hand an attacker a defended system for free; noted because "destroyed" is a
choice and not the only one.
