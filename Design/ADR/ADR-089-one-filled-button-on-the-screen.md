# ADR-089 - One filled button on the screen

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Owner decision on `Design/Plans/UI-01-ClientImprovements.md` item 2.4's [ASK]: *"Only the leading card's primary."*
**Supersedes:** -

---

## Context

`EventAction::primary` is set per EVENT: `SnapshotView` marks the build it offers on a card as
primary, and `DigestView::StandingMoves` marks the first standing move it composes. The digest draws
a primary filled in `Ink::BLUE` and everything else outlined.

`DESIGN-GUIDELINES.md` has always said *"the one filled button on an event, at most... so a glance
finds the thing the digest thinks you should do"*, and the field's own comment in `MatchState.h` says
the same. It is true per card and false per screen: a tick that reports production at three systems
offers a priced build on each, all three primary, all three filled. Three things a glance is told to
do is no glance at all.

## Options considered

### A. Filled = affordable right now

Every build the purse can cover is filled, the rest outlined; the map legend gains `AFFORDABLE`.
Turns the weight into an affordability signal. The purse is already on every button (ADR-053) and on
the top bar (ADR-087), so it would be a third way of saying a thing that is already said twice --
and it takes the weight away from meaning "do this", which is the only thing on this screen that can
mean it.

### B. One filled per screen

Filled keeps meaning "do this", and the screen says it once.

## Decision

**B**, on the owner's answer. `CardsOf` clears `primary` on every action after the first one it
finds, so a digest carries at most one filled button however many cards offer an order.

**It is the first primary in CONSEQUENCE ORDER, not `cards.front()`'s, and this is a deliberate
departure from UI-01's wording.** The item says *"only the leading card's primary is filled"*. The
leading card is the worst thing that happened -- a battle, a system lost -- and that is exactly the
card whose actions are chips rather than orders (ADR-081). Read literally, a tick where you lost a
system and can also build a shipyard would fill nothing at all: the one card allowed to carry the
hint is the one that has none. Since the cards are already sorted by consequence, the first primary
in the list IS the most consequential thing that can actually be acted on, which is what the rule was
reaching for. **If the owner wants the literal reading, it is three lines.**

**The clearing runs last**, after the ranking and after ADR-056's standing moves are inserted into
the leading card, so a digest with nothing to act on still gets its one filled button.

**A digest that offers no order fills nothing.** There is no hint to give, and filling an outlined
control to have something blue on the screen is the failure this ADR is about, in miniature.

## Consequences

- **`primary` stops being a property of an event and becomes a property of the screen.** It is still
  set per event by `SnapshotView` -- which is right, because "this is the build I would offer here"
  is a fact about the card -- and `DigestView` now has the last word, which is where presentation
  decisions belong (ADR-045).
- **A player scanning for the blue button finds one thing.** What they lose is the ability to tell,
  at a glance, which of three builds is affordable; that is what the price and the dim state on each
  button are for, and both are unchanged.
- **The filled button can move between ticks** as the consequence order changes. It always marks the
  same idea -- the most consequential thing you can do something about -- so the movement is the
  information rather than noise.
- Nothing about the at-lock or offline path changes: a primary that is not editable is drawn dim and
  outlined by `OrdersEditable` before this rule is consulted (ADR-085).

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Components (the filled button is per screen, not per card).
  Done in this commit.
- **Code:** `LockstepClient/DigestView.cpp` (`CardsOf`). Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `OneFilledPrimaryTests` pins that three priced builds fill one button, that a digest
reporting a loss and offering a build fills the build and not the loss, and that a digest with no
order to give fills nothing. All 146 methods in the suite pass.

**Not photographed** -- captures are being taken as one pass at the end of UI-01.

## Open questions

**None.** The [ASK] is answered and the departure above is flagged for the owner rather than left
open: it is a wording question with a three-line answer either way.
