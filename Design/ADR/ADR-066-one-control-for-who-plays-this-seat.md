# ADR-066 - One control for who plays this seat

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner decision, in the 2026-09-12 build prompt: *"`HUMAN | BOT` on the card and `BOT TAKES OVER | SEAT GOES CUSTODIAN` in the detail panel answer one question twice."*

**Amends:** ADR-037's "the seats screen's dead controls come alive". The three controls it named
still do what it says; two of them are now one.

---

## Context

ADR-036 drew a `HUMAN | BOT` toggle on every seat card and a `BOT TAKES OVER | SEAT GOES CUSTODIAN`
pair in the detail panel. ADR-037 made both work and described them as separate things: the toggle
is "who plays this seat" and the pair is "what happens to a seat whose player has not turned up".

They are not separate. There is one question on this screen — **who plays seat 04** — and it has
three answers: a person, a person the machine takes over from if they do not arrive, or a machine
now. The build asked it twice, in two places, in vocabularies that do not visibly belong to the same
question, and left the host to work out that `BOT` on the card and `BOT TAKES OVER` in the panel
differ by *when*.

The split also put half the answer where it cannot be compared. The card is one of six in a grid,
which is where a host reads the lobby; the panel shows the selected seat only, so the takeover
setting of the other five is visible on the card's status line (`BOT WILL TAKE OVER`) and changeable
only by selecting each seat in turn.

`GOES CUSTODIAN` is also the name of a *consequence* rather than of a choice. Nothing on this screen
sends a seat to a custodian: the custodian rules are what the simulation does to a player who stops
turning up mid-match, and choosing this option is choosing to wait.

## Options considered

### A. Leave the two controls and re-word them

`BOT NOW | BOT AT T1` in the panel would at least name the difference. It keeps the reading problem:
the answer to one question is in two places, and five of the six seats show only half of it.

### B. One three-way on the card

`HUMAN | HUMAN, BOT AT T1 | BOT`. One control, one question, and every seat's answer legible in the
grid without selecting anything.

The cost is width. The card is 212 pixels and the toggle strip inside it is 188; at 8 pixels a
glyph, `HUMAN, BOT AT T1` alone is 128 of those, so three equal thirds cannot hold it.

### C. A three-way in the panel, nothing on the card

Room for the full phrasing, and it moves the commonest control on the screen off the grid into a
panel that shows one seat at a time. Backwards.

## Decision

**B, with the middle segment reading `BOT AT T1`.**

The card carries `HUMAN | BOT AT T1 | BOT` and the detail panel's `IF STILL WAITING AT T1 LOCK`
block is gone. The segments are **sized to their labels** rather than cut into equal thirds — 52, 84
and 36 pixels for the three, with the remaining 16 split as two gaps — because a third of 188 is 62
and `BOT AT T1` needs 84.

**The label is `BOT AT T1` and not the prompt's `HUMAN, BOT AT T1`**, which is 128 pixels of a
188-pixel strip and cannot sit beside `HUMAN` and `BOT` at any spacing. Beside its two neighbours
the shorter form says the same thing: a person now, a bot at T1 if they have not come, or a bot.
`DESIGN-GUIDELINES.md`'s frame is what decides this — 1280×720 at one 8×8 font (ADR-011, ADR-014) —
and where the frame and a phrasing disagree, the frame wins.

**`HUMAN` means "wait for them".** Picking it sets `GoesCustodian`, so it takes back a takeover the
host had agreed to, which is what an exclusive three-way has to do. The state pair behind the
control is unchanged: `Kind` and `IfWaiting` together, `SeatIsReady` unchanged, and so **ENTER
MATCH's enablement rule is unchanged** — a seat is ready if it is a bot, somebody is connected, or
the host has said they will not wait.

**The middle is refused on the host's own seat**, with a refusal in the footer beside the other
refusals: the host is never the player who did not turn up. `BOT` keeps its two refusals from
ADR-037.

**`FILL WAITING WITH BOTS` is unchanged** and sets every waiting human seat to `BOT` — every seat
that is a person's and has nobody connected, whichever of the two human states it is in.

**The detail panel keeps `HOW IT PLAYS` for a bot seat** and, for a human seat, says what the card's
setting does rather than offering a second way to change it.

## Consequences

- Every seat's answer is readable in the grid. A host scanning six cards sees three states without
  selecting anything.
- **The panel is shorter by a label, two buttons and a gap**, which is room the screen did not need
  and now has.
- `GOES CUSTODIAN` as a phrase is gone from the client. The custodian rules are unchanged and are
  still what happens to a player who stops turning up mid-match; nothing on this screen chooses
  them any more, because choosing to wait was never choosing that.
- **`BOT AT T1` names a tick that this screen cannot reach.** ADR-037's open question stands: the
  conversion happens at `ENTER MATCH`, which is before T1 rather than at it, and a player who
  connects and then disappears at tick 40 still goes to a custodian.
- Three segments of eighteen pixels are three targets where there were two, in the same strip. The
  smallest is `BOT` at 36 wide, which is the same width the old half-toggle's label occupied.

## What this changes elsewhere

- **Design/:** `Design/UI/SCREENS.md` 09 and `DESIGN-GUIDELINES.md`'s seat card component.
- **Code:** `Lockstep/SeatsPage.{h,cpp}` — the card's three-way, `ACTION_GOES_CUSTODIAN` removed,
  `ACTION_HUMAN` now sets `IfWaiting`, the panel's block replaced by a sentence, and
  `BotTakesOverSeat` added so the middle state can be observed.
- **AGENTS.md:** nothing.

## Verification

`LockstepTests`: `AWaitingSeatCanBeMarkedWithoutBecomingABot` sweeps the screen and pins that a
control exists which leaves a seat a person's while marking it for a bot — the claim the middle
segment makes. `ASeatCanBeHandedToABot`, `TheHostsOwnSeatCannotBeGivenAway` and
`EveryoneIsHereOnceTheSeatsAreBots` pass unchanged, which is the claim that the other two segments
and the enablement rule did not move.

## Open questions

**Whether `BOT AT T1` should say which tick in a practice match.** The practice preset's first lock
is two minutes away rather than six hours, and the label is the same.

**Whether the host's seat should show the three-way at all.** Two of its three segments are refused,
so it is a control that is mostly a label on exactly one card.
