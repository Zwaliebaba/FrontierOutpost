# ADR-065 - The lock does not take the sheet away

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, in the 2026-09-12 build prompt: *"Today any open sheet closes at the lock. Instead: keep it, dim every row, put the filled grey `LOCKED` chip in the header where the status word was."*
**Supersedes:** - (amends ADR-039's screen 06)

---

## Context

ADR-039 built screen 06, the state the main page is in between the countdown reaching zero and the
next state arriving. Everything on it goes inert: the countdown turns grey, every action button is
dim, the rail wears a filled `LOCKED` chip and says why. `MainPage::Update` also did one more thing,
in one line: `m_panel = Panel::None`.

**A sheet is where somebody is in the middle of deciding something.** The destination picker is open
because a player is reading four lanes and choosing between an expansion and a fight; the build
sheet is open because they are weighing two buildings against a purse. The lock arriving is exactly
the moment that decision stops being actionable — and the screen answered it by deleting what they
were reading, with no explanation, on a screen whose whole purpose is to explain that the orders
have gone in.

Worse, the sheet vanishing looks like a tap that missed. When the server is on time screen 06 lasts
under a second, so the sheet appears to close by itself for no reason at all; ADR-039 notes that this
screen "is what a player sees when the server is late, which is when it matters", and that is also
when the disappearance is longest and least explicable.

`MainPage::Create` had the same problem one step further out: it is called for every state that
arrives, and it reset the panel unconditionally, so a sheet a player opened between two ticks was
taken from them by the tick landing.

## Options considered

### A. Keep closing it

One line, no state to carry, and it guarantees that nothing stale is ever on screen. It costs the
reading and gives the player no account of where their sheet went.

### B. Keep it open and let it keep working

Rows stay live, orders taken while locked go to the next tick. It is the smallest code change of
the three and it is wrong: the rail says "anything you tap now is an order for T9", and a build
sheet whose rows still queue would be queuing against a purse and a board that are about to be
replaced. ADR-053 took exactly that class of button off the screen.

### C. Keep it open and make it inert, saying why

The sheet stays, every row goes dim and stops being a target, the header wears the same filled grey
`LOCKED` chip the rail wears, and the rail's lock sentence is repeated inside the sheet in amber.

## Decision

**C.** At the lock, an open sheet stays open and says that it is locked.

**Nothing in it is a target.** The build and signal panels already passed `EventRefs::NONE` for
every row while the orders were locked, which is what draws a row dim; the destination picker did
not, so its rows looked live and did nothing — that is fixed here rather than papered over.

**The header carries the chip, clear of the `X`.** The same `LOCKED_FILL` grey the rail wears, in
the header's status position, sixteen pixels tall, ending where the `X`'s 36-pixel corner begins so
that a chip is never drawn into a target.

**The sheet repeats the rail's sentence, in amber, under its header** — *Resolving T8. Controls
return with the new digest. Anything you tap now is an order for T9.* It is the same string, from
one function, because two copies of it is one wrong tick number waiting. It sits where a sheet's
first line of content would be, so the sheet reads header, why, rows.

**`X` and `CANCEL` still close it.** They are not orders and they are the only two controls on the
screen whose meaning does not change at the lock.

**A new state rebuilds the sheet if what it was about is still there, and closes it otherwise.**
That needs the subject as an ID rather than a position: `MainPage` now keeps both, because a
position in `graph.systems` or `fleets` is a position in a FOGGED list and the tenth system a player
can see this tick may be the eleventh next tick (ADR-057). A build sheet reopens when the system is
still visible and still theirs — the same test `Action::OpenSystem` applies (ADR-058) — and a
destination picker when the fleet is still theirs. The signal picker always reopens, because its
list is recomposed from the new snapshot and is never empty (ADR-039). The replay sheet reopens on
the tick it named, which a newer tick does not make untrue.

**The armed concede is cleared by a new state**, which is new and is a consequence of this: arming
is an index into the signal list, the list is recomposed with the state, and a kept index would be a
row armed that nobody armed.

## Consequences

- Screen 06 is now a state the whole screen is in rather than a state the screen is in after
  throwing something away.
- **A sheet can be open across a tick boundary and be rebuilt from new numbers.** A destination
  picker's ETAs and a build sheet's prices update under the player, which is right — they are facts
  about the tick that is now being ordered for — and it is the first place in this client where a
  panel's content changes while it is on the screen.
- `MainPage` carries one more field (`m_panelSubjectId`) and `Create` gained a reopen path, which is
  the first thing in `Create` that is not simply "take this state".
- **A sheet that closes because its subject went is unexplained.** The player loses a system and the
  sheet about it disappears, with the digest card that reports the loss as the only account. That is
  the same silence this ADR removed at the lock, in a rarer case, and it is not fixed here.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 06 loses "any open sheet closed" and gains what it does
  instead; `DESIGN-GUIDELINES.md`'s sheet component gains the locked form.
- **Code:** `LockstepClient/MainPage.{h,cpp}` — `Update` no longer closes the panel, `Create`
  reopens it, `ReopenPanel`, `LockSentence`, the header chip and the help band in `DrawPanel`, and
  the destination rows are no longer targets while locked.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `OpenSheetTapTests` opens a destination picker on an opening board, drives the
countdown past zero and pins that the sheet is still open and that a full sweep over it queues
nothing and moves no fleet; and it opens a build sheet, pins that a new state keeps it, and that a
state in which the system has changed hands closes it. Run on the client with a `--serve` process
suspended past a lock: the sheet stays, dim, with the chip and the amber sentence.

## Open questions

**Whether the focused system should survive a new state too.** It does not — `Create` still clears
it — so a sheet can come back over a map that has forgotten what it was pointed at.

**Whether a sheet should close when the match ends.** A finished match draws no `LOCKED` chip
(`atLock` excludes it) and every row is already inert, so a sheet open at the final tick stays
open reading as a live one with dim rows.
