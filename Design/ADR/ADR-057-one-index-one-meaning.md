# ADR-057 - One index, one meaning

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, on a screenshot of a three-card digest: *"Map button does not work and why do I see the mining station DOTHAN twice?"*
**Supersedes:** -

---

## Context

Two defects on one digest, and both are a name meaning two things.

**The MAP button did nothing.** `EventAction::target` is an index, and what it indexes depends on
the action: a `QueueBuild` target is a row in `Orders::builds`, a `RedirectFleet` target is a fleet,
and a `Focus` target is a SYSTEM position. `MainPage::ActionFor` mapped `Focus` to
`Action::FocusEvent` - whose handler reads `m_state.digest[index]`, a DIGEST index. So MAP handed a
system position to code that looked it up in the digest. When the system sat past the end of a
short digest the bounds check swallowed the tap and the button did nothing at all; when it did not,
it would have focused whatever event happened to share the number. The bounds check, added after an
out-of-range read crashed the client, is what turned a wrong answer into a silent one.

**Every economy event offered the same build.** `ColorOf` collapses `SystemClaimed`, `LaneOpened`,
`LaneCanceled`, `Economy` and `OrderRejected` onto one `EventKind::Economy` - it is a COLOUR, which
is what the name says. The action composer tested that collapsed kind and attached a button for
`builds.front()` to every event wearing it, so a tick that claimed a system and paid production
carried `MINING STATION DOTHAN 15 CR` twice, on two cards, for one order.

## Decision

**1. `Action::FocusSystem` exists and MAP uses it.** Its index is a system position and it is
bounds-checked against the systems. `Action::FocusEvent` keeps its digest index and the card-wide
region that uses it. One action, one array.

**2. A build is offered on the event that would make a player want it, and never twice.** The
composer tests the entry's OWN `DigestKind`, not the collapsed colour, and remembers which rows it
has already offered:

- a claimed system offers a building AT THAT SYSTEM, which is the order the event causes;
- a production line offers whatever row is still unoffered, which is what the credits are for.

The button's target is that row rather than a hardcoded zero, so the second card queues the second
building instead of re-queueing the first.

## Consequences

- `CLAIMED PELL` now offers `SHIPYARD PELL`, and the production line beneath it offers
  `SHIPYARD DOTHAN`. Two cards, two orders, neither one a duplicate of the other.
- An economy event with nothing relevant left to build carries no build button. ADR-056's standing
  moves are the floor under that, so the digest still offers something.
- `EventKind` is now used only as what it is - a colour and a consequence rank. Anything that needs
  to know what actually happened reads `DigestKind`.

## Verification

`LockstepTests`: `NoTwoEventsOfferTheSameBuild` plays fourteen ticks and asserts no two events ever
carry the same build row and that every target names a row that exists;
`AMapButtonNamesASystemAndNotADigestEntry` asserts every `Focus` target is a valid system position -
the invariant whose violation made the button inert. Run on the client with a sixty-second tick, so
the layout could not move between captures: before the tap, `nothing queued` and a bare `MAP`
header; after one tap on MAP, `nothing queued` still and `MAP - FOCUS: PELL`.
