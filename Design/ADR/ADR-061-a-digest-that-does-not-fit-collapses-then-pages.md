# ADR-061 - A digest that does not fit collapses, then pages

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, in the 2026-09-12 build prompt: *"Cards past y=720 are currently cut."*
**Supersedes:** - (answers ADR-052's "there is still no scrolling anywhere in this client")

---

## Context

The digest is a 400-pixel column, 676 pixels tall below the top bar, and the stack of cards drawn in
it is whatever the tick produced. `DrawDigestRail` laid the cards out one after another and stopped
when the next one would start past the bottom of the screen, which means a tall digest simply ended.
`Design/UI/SCREENS.md` recorded that as a defect — *"the digest does not scroll — cards past y=720
are cut"* — and README.md item 5 repeated it.

**Two things make it worse than it sounds.** The digest is the order surface (ADR-034), so a card
that is cut is not merely unread: its `ACCEPT`, its `REDIRECT` and its priced build are unreachable
for the whole tick. And a returning player gets the biggest digest there is — ADR-044 concatenates
every tick they missed into one list — so the case where cards are lost is exactly the case where
the most has happened.

**Nothing in this client scrolls, and that is a decision rather than an omission** (ADR-052 option C):
there is no scroll model, and a gesture in the digest would compete with the map drag beside it
through a `PointerInput` that has no concept of a region owning a gesture.

The stack is also taller than it needs to be. An actor card carries one line per event the rival
produced (ADR-034's grouping) — three, five, eight lines about somebody the player may not be
interested in this tick — and it is the only card whose body is a *list* rather than a sentence.

## Options considered

### A. Scroll the digest

Every card reachable, no new vocabulary, and the thing every other program does. It is ADR-052's
option C at a second site, with the same cost: a scroll model this tree does not have, and a drag
that has to be arbitrated against the map's. Taking it here would mean taking it everywhere, which
is a bigger decision than one column.

### B. Shrink the cards until they fit

Smaller line height, fewer detail lines, a cap on cards. It needs no new control at all. It fails on
the case that matters: a backlog of four ticks is not a formatting problem, and a cap is the cut
this ADR exists to remove, moved one step earlier.

### C. Collapse what is compressible, then page what is left

An actor card shows its title, its stamp, its verdict and its actions, and opens on a tap. If the
stack still does not fit, it is paged, with a band at the foot of the column saying which page this
is and offering the next.

Two mechanisms rather than one, which is the cost. What it buys is that the first one is free — a
collapsed actor card loses no fact, because the title names the rival, the stamp counts their events
and the actions are all still there — and the second is only reached when a tick genuinely produced
more than a column of reading.

## Decision

**C.** An actor card is collapsed unless it is the open one, and a stack that still does not fit is
paged.

**Collapsing is for actor cards and nothing else.** They are the only cards whose body is a list, and
the only ones that can lose their body without losing a fact the card is the sole record of — an
event card's detail is the event. A collapsed card keeps the dot, the uppercased title, the
`3 EVENTS` stamp, the verdict box when it has one, and the whole action row, which is what keeps
ADR-034's "every event carries its own actions" true of a card that is closed. **The title is the
control**, a 22-pixel band — the same height as a section header, which is what this screen already
uses for a label that is also a control — and **one card is open at a time**: a column that holds one
card's worth of lines should not be able to hold two open and page them apart.

**Paging is measured, not guessed.** Every card's height is worked out before anything is drawn,
because a page break has to fall *between* two cards and the only way to know where one ends is to
have measured it. `CardLayout` is that measurement and the draw reads it back rather than recomputing
it, so the wrap happens once and the two cannot disagree. A page always takes at least one card, even
one taller than the column: a card that fits nowhere is still better read cut off than not drawn.

**The band is 22 pixels at the foot of the digest**, and it reads `1 / 3 - MORE >` — numbers first,
` - ` between facts — with `< PREV` on the left once there is a page to go back to. It is drawn only
when the stack does not fit, so a two-card digest looks exactly as it did.

**The page resets to one when a new digest arrives**, along with the open card: a digest is replaced
wholesale every tick, page three is nowhere in the new one, and the rival whose card was open may
have no card at all.

**The standing moves are always on page one** (ADR-056). They are attached to the leading card and
page one starts at the leading card, so this holds by construction rather than by a rule — which is
the reason the page breaks are computed forwards from the top and not fitted from anywhere else.

## Consequences

- **No card is unreachable any more.** That was the point, and it is the first time it has been true
  of this column.
- **There is still no scrolling in this client.** ADR-052 option C stands; this is paging, and the
  band is a control rather than a gesture, so nothing competes with the map's drag.
- A player reading a rival's card has to tap to see the lines. That is a real cost and it is paid by
  the card least likely to be about the thing that just cost them something — an actor card is
  ranked by its worst event, so the one that matters is at the top where the title and the verdict
  are, and both survive collapsing.
- **The page band is 22 pixels and ADR-052 says a target is 44.** The rail's `SIGNALS` header is the
  same 22 and has been a control since ADR-039, so this is consistent with the screen rather than
  with the sheet; ADR-052's open question about the rails' row height now covers three controls
  instead of one.
- The digest header still counts every event, not the ones on this page. `7 EVENTS` is a fact about
  the tick and paging is a fact about the screen.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 01 loses "the actor card is always expanded" and "the digest
  does not scroll — cards past y=720 are cut"; README.md item 5 loses the same two claims;
  `DESIGN-GUIDELINES.md` records the collapsed actor card and the page band.
- **Code:** `LockstepClient/MainPage.{h,cpp}` — `CardLayout`, `LayoutCard`, the page walk in
  `DrawDigestRail`, and two actions. `DigestView` is untouched: what a card IS did not change, only
  how much of it is drawn.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `DigestOverflowTapTests` opens and closes an actor card from its title, pins that a
second card opening closes the first, and on a twenty-card digest pins that a tap reaches page two
and another comes back to page one. Run on the client against a backlog: `SINCE YOU LOOKED - T6 > T9`
pages, and the card carrying the standing moves is on page one.

## Open questions

**Whether an event card should collapse too.** It has one detail line and collapsing it would save
twelve pixels for the loss of the sentence, which is not a trade worth making today — but a digest
of twenty single-line cards pages for want of a shorter card rather than for want of room.

**Whether the open card should survive a new digest.** It does not, and for a backlog that is
right — the cards are different cards. For a rival a player is watching across ticks it is a tap
they pay every tick.
