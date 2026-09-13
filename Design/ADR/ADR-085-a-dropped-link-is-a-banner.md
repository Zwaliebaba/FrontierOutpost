# ADR-085 - A dropped link is a banner, and it takes the orders rather than the board

**Status:** Accepted

**Date:** 2026-09-14
**Decided by:** Build session, implementing `Design/Plans/UI-01-ClientImprovements.md` item 1.5 at the owner's instruction to work the plan.
**Supersedes:** - (amends ADR-038's dialog for one of its kinds)

---

## Context

`ConnectionDialog::Kind::Lost` is a full modal: a scrim over the whole screen, a centred card, and a
`HandleTap` that returns true for every pixel so nothing behind it can be touched (ADR-038).

Its own copy says *"Anything you tap while this is up is not sent."* That sentence is true and it is
the argument against the modal. **The things a player can still do while the link is down do not
need sending**: reading the digest that arrived before the drop, looking at where a rival's fleet
got to, opening a build sheet to see what a level costs, orbiting the map. The modal told them
nothing they tapped would count and then stopped them doing the things that never needed to.

Two copy defects sat inside it. `Reconnecting - back 4 time(s) already` shipped with the placeholder
plural still in it, on the one screen whose job is to look dependable. And
`The tick still locks in 00:00:00 whether or not you are back` is a countdown that has stopped
counting: past zero the tick is gone, and what the player needs to know is that it went without
them.

## Options considered

### A. Keep the modal

It is unambiguous -- nothing behind it is reachable, so nothing behind it can be misread as
reachable. That is worth something on a screen whose whole discipline is "an order is an order until
the lock". What it costs is every read-only thing on the board, for as long as the link is down,
which on a bad connection is most of a tick.

### B. A banner, and the board stays live

The band says what the card said; the page dims what reaches the wire. It needs the page to know it
is offline, and it needs the guard to be real rather than visual -- a control that merely LOOKS
inert and still composes an order is worse than the modal.

## Decision

**B.** `Kind::Lost` draws a 44px amber-bordered band under the top bar spanning all three columns,
with no scrim, and swallows only its own two buttons. Every other kind stays exactly as it was.

**44 pixels, because that is the top bar's height and the sheet row's.** The band reads as another
row of the frame rather than as something laid over it, which is the whole point: what is under it
still works.

**`ConnectionDialog::Modal()` is the new question, and `Visible()` keeps its old meaning.** The match
loop gates the drag and the wheel on `Modal()` and draws on `Visible()`. A banner returning false
from `HandleTap` for anything but its buttons is what lets the board see the tap at all.

**`MainPage::OrdersEditable()` is the guard, and it is one question asked in one place.** It is
`!locked && !finished && !offline`, and every control that composes an order now asks it -- the
digest's buttons, the build sheet's rows, the destination picker, the signal picker, the concede.
It replaced twelve sites that each asked `!m_state.orders.locked` and none of which had ever heard of
the third condition. **The guard is on the target, not the paint**: a row that cannot be ordered is
given `EventRefs::NONE` and is therefore not a hit at all, which is why `OfflineBoardTapTests` can
sweep the entire screen and find no order composed.

**`SetOffline` is a page call and not a `MatchState` field.** It is a fact about this client's
socket rather than about the match; a snapshot carrying it would be a snapshot that could disagree
with the wire.

**A sheet shows `OFFLINE` where it shows `LOCKED`**, and its help line says the same thing in the
same slot ADR-065 and ADR-078 already share. The two are the same shape of statement -- this sheet
is showing you something it cannot take an order about -- and differ only in why.

**Both copy defects are fixed**: `back once already` / `back 4 times already`, and past zero the
banner reads `T10 locked while you were away.`

## Consequences

- **The board is live during a drop, and the player can compose nothing.** That is the trade: they
  get to read, and they get no false sense that an order taken now will land. The controls are dim
  rather than absent, so what they can do when the link returns is still visible.
- **`OrdersEditable` folds three conditions into one call, and one of them is new to most sites.**
  A control added in future gets the offline case for free, which is the point; a control that wants
  to be live offline now has to say so explicitly.
- **`Kind::Lost` no longer blocks a tap that reaches the map and opens a sheet.** A sheet opened
  while offline stays open when the link returns (ADR-065's restore), which is the desirable end of
  that.
- The banner shows less than the card did: one line rather than four. What it drops -- that orders
  already sent still count -- is true whether or not it is on the screen, and the card had four
  lines because it had a whole screen to fill.
- **ADR-038's "every connection state is one component" still holds.** One class, one `Compose`, two
  presentations.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 04, `DESIGN-GUIDELINES.md` §Components (the dialog gains a
  banner form). Done in this commit.
- **Code:** `LockstepClient/ConnectionDialog.{h,cpp}` (`Modal`, `IsModal`, `DrawBanner`,
  `Facts::lockedTick`, both copy fixes, the tap rule), `LockstepClient/MainPage.{h,cpp}`
  (`SetOffline`, `OrdersEditable`, twelve guards, the sheet chip and help line),
  `Lockstep/Lockstep.cpp` (the loop gates on `Modal()` and sets the page offline). Done and built.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `ALostLinkIsABannerAndNotAModal` pins that a dropped link draws something, is not
modal, and lets a tap on the map and on the digest through; `TheBannerStillOffersRetryAndQuit`
presses both. `AModalSwallowsEveryTapItIsOver` is the old test, moved onto `Refused` -- it was
written against `Lost`, which is exactly the behaviour this changes. `OfflineBoardTapTests` sweeps
the whole screen with the page offline and pins that no tap composes any order of any kind, that a
sheet still opens and a system still focuses, and that clearing the flag brings the controls back.
All 138 methods in the suite pass.

**Not photographed** -- the desktop was locked for this session, so `04-connection-lost.png` is
stale and shows the modal.

## Open questions

**Whether the banner should push the board down rather than cover it.** It covers the top 44 pixels
of all three columns, which is the digest's header row, the map's `MAP - FOCUS` line and the rail's
`LOCKS Tn`. None of those is a control; all three are things a player might want while deciding
whether to wait. Pushing the frame down instead is a layout change to every pane and was not worth
it for a state that is meant to be brief.

**Whether a sheet should stay open across a drop at all.** It does, and it is inert with an `OFFLINE`
chip. The alternative -- close it, because the thing it was going to do cannot be done -- is what
the modal effectively did, and ADR-065 already argued the other way for the lock.
