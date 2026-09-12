# ADR-041 — The buttons get pressed

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, "ok proceed", 2026-09-12, on the open question ADR-040 left.

---

## Context

Three ADRs in a row shipped controls that had never been activated. ADR-037's `BOT` toggles,
ADR-038's five connection-dialog buttons, ADR-039's signal picker and its two-tap concede: wired,
compiled, photographed even — and never once pressed, because the only thing that could press them
was a synthetic tap on an unlocked desktop, and the desktop stayed locked.

**"It draws correctly" and "it does the right thing when you hit it" are different claims**, and
only the first was being made. A screenshot of a row reading `TAP AGAIN TO CONFIRM` says nothing
about whether the first tap already conceded the match.

The obstacle was never the tap. It was that every page in this game **builds its hit list while it
draws** — `AddHit` sits beside the `FillRect` that put the button there, which is exactly what stops
the two drifting apart — and drawing needed a GPU. An append writes through a pointer into an upload
heap, and with no device that pointer is null.

That left two bad options: stand up D3D12 inside a test DLL that CI runs on a machine with no GPU,
or write each screen's layout out a second time in the test and assert against a copy of the thing
under test.

## Decision

**`ShapeRenderer::CreateHeadless` and `FontRenderer::CreateHeadless`**: the renderer records the same
geometry into a vector instead of an upload heap. Nothing about the append path changes — it is the
same code writing to a different address — so **what a test drives is what ships**. `Flush` is fatal
on a headless renderer, because a caller that reached it believes it is drawing to a screen.

This is a seam in library code that the game never takes, which is a cost worth naming. It buys the
only thing that makes an interface layer testable at all, and it duplicates nothing. It also makes
the renderers themselves reachable by a test for the first time: `NeuronClientTests` could never
construct one.

**The tests sweep rather than aim.** They tap a grid over a region and look for the effect, the way
`Build/TapRehearsal.ps1` scans the screen for a button rather than being told a coordinate. A test
carrying a hardcoded pixel would have to be edited every time the layout moved, and it would be
asserting the layout instead of the behaviour. What these claim is *"somewhere on this screen there
is a control that does this"*, which is the claim that was missing.

Three things the sweep needs, each of which is a true fact about the screens:

- **Redraw between taps.** A tap changes what is on the screen and the hit list belongs to the frame
  it was built in.
- **Sweep upward for a panel's rows.** A panel carries its close button at the top, so a downward
  sweep shuts it before reaching a row.
- **Put the screen back.** A stray tap on the map opens the BUILD panel over the picker, and nothing
  in the sweep's path would put it back.

## What it found: `TAKE SEAT` was lying

The seats screen had a `TAKE SEAT` button, and the sweep pressed it — then handed the host's own
empire to a bot.

`TAKE SEAT` moved `m_hostSeat`, which is what decides **whose seat is protected from the `BOT`
toggle** and which seats `FILL WAITING` skips. It moved nothing else. The host's actual seat is
fixed by the token their client presented before the screen even opened — the host logs into its own
lobby with the first token it generated (ADR-036 as amended) — and `RunSeatsScreen`'s return value
is read only as a *"did the window close"* flag. So a host who took another seat could then bot the
seat they were actually playing, and enter a match a machine was playing for them.

**Removed rather than repaired.** Making it real means reconnecting with a different token, which is
the composition root's to do and not this screen's. The detail panel now says which seat is yours
and why: you logged in with this token.

ADR-036's consequence — *"the host stops being player zero by accident; they choose a seat like
everybody else"* — is still true, and it is the LOBBY that makes it true, not this button.

## Consequences

**Fifty-one tap tests**, covering every control the last three ADRs left unpressed: `EDIT TOKEN`,
`RETRY` on a refusal and `RETRY NOW` on a lost link (which are two different retries and were
already known to be), `QUIT`, `VIEW LAST DIGEST`, `CANCEL`, the scrim swallowing everything it
covers, the `BOT` toggle, `FILL WAITING WITH BOTS`, `NEW TOKEN`, the signal picker, and the two-tap
concede — first tap arms and does not send, second queues, third takes it back.

**Two negative tests that matter as much as the positive ones.** A locked screen accepts no order
anywhere on it, which is screen 06's claim that "inert" means more than "grey". And a dialog with
`canGoBack` false offers no `BACK` anywhere, which is the rule that a button must not return to a
screen this process left a match ago.

**`RunClangTidy` found float loop counters** in the first draft of the sweeps. They count in whole
pixels now, which is what a tap is.

## What is still verified by photograph, and why that is right

The drawing. These tests prove a control is where the layout puts it and does what it says; they
cannot prove the layout is any good. A `DrawWorld` produces pixels nobody can assert about, and
"the neutral dialog's title is unreadable at 26 alpha" (ADR-038) was found by looking at one.

The line stands where ADR-040 drew it, with one word changed: **if it decides, it is tested; if it
draws, it is a screenshot** — and pressing a button is deciding.

## Open questions

**`JoinPage` is not in this yet.** It is the one screen that types, and its `TextField`, masking and
caret are reachable the same way now. It was left out to keep this pass to the debt it was paying.

**A sweep is slow.** Fifty-one tests take about thirty seconds, nearly all of it redrawing a page
between taps. That is affordable now and will not be at ten screens; the fix, when it is needed, is
a sweep that draws once and re-drives only the pages a tap actually changed.
