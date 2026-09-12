# ADR-058 - A build sheet is about one system

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, on a screenshot of the build sheet: *"When I select DOTHAN I see the actions I can take at Pell, but I select DOTHAN. Even when I select an enemy station I see this list. It should only show the actions at the station and nothing when I select an enemy location, right?"*
**Supersedes:** -

---

## Context

`Orders::builds` is the WHOLE empire's list of available buildings. That is what it is for: the
orders rail counts it (`4 AVAIL`), the digest offers from it, and `OrdersOf` turns the queued
entries into orders. One list, several readers.

`DrawPanel`'s build sheet was one of those readers, and it drew the list whole under a title naming
a single system. Selecting Dothan produced `BUILD - DOTHAN` listing `Mining station - Dothan`,
`Shipyard - Pell` and `Mining station - Pell` - three rows, two of them about somewhere else. The
rows were not wrong and queueing one did the right thing; the TITLE was a lie about what the list
contained, and a player picking the second row got a building at a system they had not selected.

Selecting a rival's system opened the same sheet. You cannot build on somebody else's ground -
`Match::Validate` refuses it as `NotYourSystem` - so that sheet was a list of orders that system
can never take, under a title naming it.

## Decision

**1. The sheet lists the selected system's buildings and nothing else.** The filter is
`row.system == node.id`, and those are two different numbers for the same system: `BuildRow::system`
is a system id, `m_panelSubject` is a position in the view's own fogged list. The node carries
both, which is why the comparison goes through it (ADR-057 is the same confusion, one screen over).

**2. A system with both buildings says so** rather than opening an empty sheet.

**3. A system you do not hold opens no build sheet.** Tapping it focuses it, because a tap that
does nothing visible is a defect this screen has been bitten by twice; what it does not do is offer
an order.

**4. `BUILDS N AVAIL` on the rail still counts the empire.** It is a census and says so; the sheet
is a selection. That distinction is the same one `totalSystems` makes against the visible map.

## Consequences

- Reaching every building now takes one tap per system rather than one tap and a scroll, which is
  what the sheet's six-row limit wanted anyway.
- A rival's system is now inspectable (it focuses) but offers nothing. When there is something to
  DO about a rival's system - a proposal, a target - it belongs on that system's own sheet, and
  this decision does not write it.

## Verification

`LockstepTests`: `ARivalsSystemOpensNoBuildSheet` sweeps the screen for a system somebody else
holds, and asserts that tapping it focuses the system and opens no build sheet. The filter itself
is drawing and was verified on the client: holding Dothan and Pell with `4 AVAIL`, the sheet for
Dothan lists exactly `Shipyard - Dothan` and `Mining station - Dothan`, and tapping P2's Torvald
opens no sheet at all - only `MAP - FOCUS: TORVALD`.
