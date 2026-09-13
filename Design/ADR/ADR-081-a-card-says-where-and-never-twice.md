# ADR-081 - A card says where, and never says it twice

**Status:** Accepted

**Date:** 2026-09-13
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 1.3 at the owner's instruction to work the plan.
**Supersedes:** -

---

## Context

`SnapshotView::ViewOf` gives every digest entry with a system a `MAP` button that focuses it. That
is one button per EVENT, and the cards those events fold into carry all of them.

On `Design/UI/screens/01-main-page.png` the result is visible twice over. A collapsed actor card
reads `P4 · 5 EVENTS` above four buttons, every one of them labelled `MAP` — four different places
behind four identical labels, on a card that says neither what P4 did nor where. And every plain
event card carries one too: `CLAIMED HOLLIS` with a `MAP` button that focuses Hollis, on a card
whose own body already focuses Hollis when tapped (`Action::FocusEvent` follows `leadEvent` to the
same `refs.system`). That is one tap drawn twice, on most cards in the digest.

ADR-056 already treats `Focus` as not counting toward whether a card can be acted on — *"it moves
the eye and gives no order"* — so these buttons were known to be weak. What had not been noticed is
that half of them do nothing at all.

## Options considered

### A. Leave them

They work, and `MAP` is a word a player learns once. The cost is the actor card, which is the densest
thing in the digest and the one most in need of saying what it is about; four identical labels is
the worst possible use of the row it takes.

### B. Label each `MAP` with the system, keep them all

One line of change, and it fixes the actor card. It leaves the redundant one on every plain card,
which is the half that is not merely weak but inert.

### C. Name them, drop the redundant one, and cap the count

Each distinct system a card points at becomes a chip labelled with its name; the one that points
where the card already goes is dropped, because the card body is the control for that; four at most,
then `+n`.

## Decision

**C.** `DigestView::NameTheTargets` rewrites every card's `Focus` actions after assembly and before
ranking: distinct targets only, named for the system, four chips and then `+n`, and nothing pointing
where the card's own tap already goes.

**The overflow chip focuses the first system it stands for** rather than being inert. A control that
says there is more and does nothing when pressed is the defect this screen has been bitten by twice
(ADR-057's `MAP` reading the wrong array, ADR-053's button the lock was certain to refuse), and the
rest of the systems are on the card's own per-event lines either way.

**A `Focus` whose target names no system on this map is dropped, not drawn.** It cannot be labelled,
`MainPage` bounds-checks it into a no-op anyway (ADR-057), and a button that does nothing is the
thing this ADR is removing.

**Plain cards get named chips too, not just actor cards.** UI-01 asked only that the redundant one
be dropped from them, but a plain card whose surviving target is a DIFFERENT system — a production
card offering a build somewhere else — is better off naming it for exactly the reason an actor card
is. One rule, applied to every card, rather than two rules that have to agree.

## Consequences

- **A card's action row is wider than it was**, because `HOLLIS` is longer than `MAP`. The digest
  drops buttons that do not fit rather than wrapping them, so a card pointing at four
  long-named systems can lose its last chip — which is the same cap `+n` exists for, reached a
  different way. The controls that give orders are composed first and so are never the ones dropped.
- **`Design/UI/screens/` captures are stale in a new way**: every card in every main-page capture
  shows `MAP` buttons that no longer exist.
- The chips make a card's systems tappable individually, which the per-event lines never were.
- Nothing about what a card focuses changed, only which buttons offer it. `Action::FocusSystem` and
  its bounds check are untouched.

## What this changes elsewhere

- **Design/:** `DESIGN-GUIDELINES.md` §Components (actor card, event card), `SCREENS.md` 01.
  Done in this commit.
- **Code:** `LockstepClient/DigestView.cpp` (`NameTheTargets`, called from `CardsOf`). Done and
  built. `SnapshotView` still composes a `MAP` per event and is unchanged: what a card points at is
  a fact about the event, and which buttons to draw for it is presentation (ADR-045).
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `NamedTargetTests` pins that an actor card about three systems names the two its
own tap does not reach and carries no button labelled `MAP`; that a plain `CLAIMED HOLLIS` card
keeps no chip at all; and that six systems become four chips and a `+1` that still points somewhere.
`MergedRepeatTests::AMergedRunKeepsEveryActionOnce` gains an assertion that two events at one system
fold to one chip. `DigestViewTests`' fixture gained a six-system graph, because a chip is named for
the system it points at and a fixture with no systems is one where every target points nowhere.

**Not photographed** -- the desktop was locked for this session.

## Open questions

**Whether the chips should be outlined or plain text.** UI-01 asked for an outlined chip and they
are drawn as the digest's ordinary outlined buttons, which is the same thing by another name; if the
action row ever gets a second visual weight, a focus chip is the first candidate for the lighter one.
