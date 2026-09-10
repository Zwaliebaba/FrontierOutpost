# ADR-021 — Combat is simultaneous, proportional, and integer

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Build session for `Design/Plans/4X-01-CoreLoop.md`, Step 5. The plan names this ADR in §3 and says to write it in the step that meets it.
**Supersedes:** —

---

## Context

The one-pager fixes the *shape* of combat and gives no numbers:

> **4b, system combat**, read from the post-4a state, at every system holding hostile fleets: one
> melee, each fleet's damage spread across enemies in proportion to strength, fixed rounds, integer
> arithmetic. An incumbent fleet gets the defender bonus; simultaneous arrivals at an empty system
> get none. A tie is mutual attrition, not a coin flip.

And elsewhere: *"deterministic: uncertainty comes from what humans ordered, not from dice"*, and
*"processing order can never change an outcome"*.

That last clause is the constraint that decides most of this ADR. At a system with three empires
present, "processing order can never change an outcome" rules out any scheme where one fleet fires,
losses are applied, and the next fleet fires into the reduced enemy — because then whichever fleet
the loop reached first has an advantage that no player chose and no rule describes.

There is a second pressure, from the client. The orders rail previews a fight before it happens
(`Design/Screens/README.md`: *"preview: 14 v 11 (+def) · 6 left"*). A preview is only honest if the
resolver is a pure function of the visible numbers, which means no hidden state and no randomness
at all — not even a seeded draw.

`Design/Plans/4X-01-CoreLoop.md` adds a warning worth repeating: **do not pick numbers that only
work for the fixture's "14 v 11 (+def) → 6 left"**. The reference is a drawing of one plausible
tick, not a specification.

## Options considered

### A. Sequential resolution: each fleet fires in turn, losses applied immediately

The obvious implementation, and the one most wargames use. It is also the one the one-pager
forbids in as many words. With two sides it merely gives the first mover an edge; with three it
makes the outcome depend on player id, which is an ordering nobody agreed to. Rejected on the
design's own terms.

### B. Simultaneous rounds: every side's output computed from round-start strength, applied together

Each round reads the strengths at the start of the round, computes what everyone deals, and applies
all of it at once. Nobody fires first. A tie is then a tie by construction rather than by a special
case — two identical sides compute identical output and take identical losses, which is exactly
"mutual attrition, not a coin flip".

It is also the same discipline as ADR-019 one level down: read a snapshot, write a new one.

### C. A closed-form solution: solve the attrition equations, skip the rounds

Lanchester-style. One formula, no loop, and it would be faster.

Rejected for two reasons. It needs real arithmetic to be meaningful, and R16 makes `GameLogic`
integer end to end; an integer closed form is an approximation of an approximation. And it deletes
the round structure, which is the thing Phase 0 will actually want to tune — "fixed rounds" is a
knob a designer can reason about, where an exponent is not.

### D. Damage as a flat number per ship, rather than a fraction of strength

Simple to explain: each ship kills *n* enemy ships a round. It makes a battle a pure race to bring
more ships, with no diminishing returns and no reason ever to split a fleet. The proportional form
at least means a fleet that is losing also hits less hard, which is what makes a defender bonus
worth having and what makes reinforcing a losing fight a real decision.

## Decision

**B, with the parameters below, all of them in `MatchRules` and all of them initial.**

One melee at every system holding fleets of two or more players. For each round, in order:

1. **Effective strength** per side: its ships, times `defenderBonusPercent` if it is an incumbent,
   integer-divided by 100.
2. **Output** per side: its effective strength times `damagePercentPerRound`, over 100.
3. **Incoming** per side: for every other side's output, the share proportional to this side's
   effective strength among that attacker's enemies.
4. **Apply** all of it, capped at the ships each side has.

Steps 1–3 read only round-start values; step 4 is the only write. The loop stops early when one
side is left standing, or when a round would change nothing.

| Parameter | Initial | What it means |
|---|---|---|
| `combatRounds` | 3 | A fight cannot run long because it is close |
| `damagePercentPerRound` | 50 | Half of effective strength, dealt per round |
| `defenderBonusPercent` | 125 | The incumbent's multiplier |
| `rearGuardEnabled` | false | Sub-phase 4a, built and switched off |

**An incumbent is a fleet that was already at the system — not the system's owner.** That
distinction is the one-pager's, and it is why `MatchFleet` carries `arrivedThisTick` rather than
deriving incumbency from ownership: at an empty system nobody owns anything, and the rule still has
to say that simultaneous arrivals get no bonus.

**Losses land on the largest fleet of a side first**, ties broken by fleet id. Which fleet absorbs a
loss changes what survives, so the order has to be total and not incidental (ADR-018).

**Sub-phase 4a is implemented and off.** The one-pager: *"switched off until Phase 0 shows dancing
dominates; if enabled, dancing stays possible and stops being free."* A switch that has never been
on is a switch that does not work, so it is built now, tested in both positions, and defaults to
false. When on, a fleet that departs a system a hostile arrives at takes one round at the ordinary
rate from the arrivals' end-of-movement strength, with no defender bonus for anyone — it is not
defending anything and they have not landed on anything.

**`MatchRules::Check` refuses combat that decides nothing** — no rounds, or no damage — and a
defender bonus below 100%, which would invert incumbency. Those are not tunings, they are settings
under which the mechanic stops existing.

### On the reference preview

With the parameters above, fourteen attacking eleven with the defender bonus leaves **six**, which
is exactly what `Design/Screens/README.md` draws. That was not aimed at: the three numbers were
chosen for shape — half strength a round, a quarter bonus, three rounds — and the arithmetic
happened to land there. It is recorded as a check on the implementation and **is not a
requirement**. If Phase 0 moves a parameter, the expected value in `CombatTests` moves with it; the
parameter does not move to preserve the six.

## Consequences

**A preview is exact.** The client can be shown what a fight will do, because the resolver has no
inputs the client cannot see. That is the seam ADR-018 opened and this ADR does not close — where
the preview is *computed* is still the server (4X-01 §3), but nothing about combat prevents an
exact one.

**Big fleets beat split fleets, but not for free.** Proportional spread means a large enemy absorbs
a large share, so splitting to bait damage does not work; but a side that is losing deals less, so
reinforcing late is worse than arriving together. Both of those are decisions the one-pager wants
players to have.

**Three-way fights favour the largest.** Two smaller empires meeting at a contested system both
shoot at each other as well as at the leader. Whether that is right is a Phase 0 question — it may
turn out to need the leader-ganging the one-pager wants from public score, or it may make the
frontier a place only the leader goes.

**Integer division truncates, everywhere and deliberately.** Small fleets round down to dealing
nothing, so a two-ship fleet cannot grind down a fifty-ship one over enough rounds. The early exit
when a round would change nothing exists so that this is a stalemate rather than a loop.

## What this changes elsewhere

`MatchRules` grows five fields. `MatchFleet` grows `arrivedThisTick` and `departedFrom`, both in
the hash — a value the resolver reads and the hash ignores is a value that can drift between two
machines with no test noticing.

Nothing in the client changes yet. The fixture's preview string is authored text; wiring a real
preview to it is Step 8.

## Open questions

**Whether three rounds is enough for a big fight to resolve.** Two large equal fleets end a tick
attrited and still facing each other, which starts a siege neither can finish. That is arguably
the interdiction the one-pager wants and arguably a stalemate generator, and only playtesting can
say which.

**What happens to a fleet reduced to zero ships when its owner has no others.** Today it is marked
destroyed and the player continues with nothing. The one-pager's *Gone* state says the fleet
reaching zero ends the player, and that is Step 7's to implement.

**Whether the rear-guard should apply to a fleet that arrived and left in the same tick.** It
cannot happen today — a fleet that arrives has no orders left this tick — but it will the moment
multi-lane movement exists.
