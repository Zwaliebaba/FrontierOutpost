# LockStep: Universe — the client's screens

What the client shows, screen by screen, and how far each screen is built. The design arrived on
2026-09-11 as a UI handoff — eight mockups at 1280×720, guidelines, a per-screen spec and a work
plan ("v2", superseding the v1 `Design/Screens/`). Most of it has since been built, some of it
deliberately not, and this directory is now the **design record for the client**, kept against the
code in `LockstepClient/` and `Lockstep/`. Every status below is as of **2026-09-13** and was read
from the tree, not from the plan.

> **`screens/` is PART RETAKEN as of 2026-09-13, and the split is not arbitrary.** FONT-01 stages 5
> and 6 are done — Plex Sans is assigned, the layout is re-derived, coverage is gamma-corrected — so
> these five are current and show what the client actually draws:
> `01-main-page`, `03-join`, `04-connection-lost`, `05-connecting`, `05-refused-unknown-token`.
>
> **The other eighteen still show the 8×8 bitmap font.** Every one of them needs the client driven to
> a state — a sheet opened, a row hovered, a replay stepped — and the client takes input through the
> Windows Pointer API, so only a real `SendInput` tap on an **unlocked desktop** reaches it
> (`Build/TapRehearsal.ps1` says why). The desktop was locked when stage 6 ran. Retaking them needs a
> session at the machine, not more code. Until then, read those eighteen for layout and content, not
> for typography.

## Contents
- `SCREENS.md` — per screen: what is built, what the design asked for that is not, and where the code is.
- `DESIGN-GUIDELINES.md` — frame, font, palette, components, copy and map rules **as built**, with the design's intent kept where the build stops short of it.
- `screens/` — **captures of the running client**, one per screen and state, taken 2026-09-12 from the Debug build of the tree at a4c9235 and retaken 2026-09-13 at 9b25816 wherever the changes since reach them (PNG, 1280×720, the client area exactly as drawn). The eight mockups of 2026-09-11 are no longer in the tree (owner decision, 2026-09-12): the build superseded 03, 04, 05 and 06, and what the other four still asked for — the map's unbuilt details, screen 02, 07's step-through, 08's tabs — is kept in words under "What the design asks for that the tree does not have" and drawn in the live design reference below. The files themselves are in the history up to a4c9235. The table below names each capture.
- The handoff's work plan, `PROMPT.md`, is finished as far as it is going to be and lives in [`Design/Archive/2026-09-11-ui-v2-prompt.md`](../Archive/2026-09-11-ui-v2-prompt.md) with a note per step. ADR-034, ADR-038 and ADR-039 cite it by its old name.
- Live design reference: the project file `Frontier Outpost Main Page.dc.html` (all mockups on one canvas, and since 2026-09-12 the only place they are drawn; design reference, not production code). **The filename predates the rename to LockStep: Universe and is deliberately left alone** — the file lives outside this repository, so renaming it here would break the pointer without renaming anything (ADR-035).

Built from the one-pager (`Design/Archive/space-4x-one-pager-v10.md` since 2026-09-13), ADR-014
(interface layer), ADR-027 (owner colours), ADR-028/029 (roles, tokens) and the decisions since:
ADR-034 (what the handoff left open) and ADR-036 through ADR-071, which are cited where they apply.

## Screens

| # | Screen | Status | In `screens/` | Code |
|---|---|---|---|---|
| 01 | Main page — digest as order surface, map, locks rail | **Built** (ADR-034; then 045, 052, 053, 055–066) | `01-main-page.png` (tick 2 of a practice match: a contact, a claim with its priced build, production), `01-orders-queued.png` (tick 0: a move ordered and a build queued, before the lock), `01-fleet-under-way.png` (tick 1: the route and its marker), `01-rail-hover.png` (the locks rail with the pointer on a row, ADR-060), the sheets `01-build-sheet.png`, `01-destination-sheet.png`, `01-signal-sheet.png` and `01-signal-sheet-armed.png` (the concede armed under its band, ADR-064), `01-build-rising.png` (a build in flight, one frame carrying all three places it is said: the rail's row, the sheet that will not take another order, and the digest's card, ADR-070), and `01-finished.png`. The map's unbuilt details are item 6 below | `LockstepClient/MainPage.cpp`, `DigestView.cpp`, `MapRender.cpp` |
| 02 | Share tick — 480×640 export | **Not built.** No button, no export; ADR-034 §2 says clipboard-only if it is ever built, and its open question asks whether it should be | none; the mockup that was the target, if it stays one, is in the design file and the history | — |
| 03 | Join | **Built** (ADR-034 §3, ADR-041) | `03-join.png` | `LockstepClient/JoinPage.cpp`, `NeuronClient/TextField.h` |
| 04 | Connection lost | **Built** (ADR-038, ADR-043) | `04-connection-lost.png` | `LockstepClient/ConnectionDialog.cpp` |
| 05 | Connection states | **Built** as one component with seven states. The WELCOME dialog is not one of them | `05-connecting.png`, `05-refused-unknown-token.png`, `05-refused-seat-in-use.png`, `05-waiting-for-the-host.png`, `05-match-finished.png` | `LockstepClient/ConnectionDialog.cpp` |
| 06 | At lock | **Built** (ADR-039, ADR-065) | `06-at-lock.png`, `06-at-lock-sheet.png` (a sheet left open across the lock: dim rows, the `LOCKED` chip in its header, the rail's sentence in amber) | `LockstepClient/MainPage.cpp` |
| 07 | Replay — phase step-through | **Stub**, and its title says so (`REPLAY TICK 7 - NOT YET WIRED`). A sheet listing the six phases; nothing steps | `07-replay.png` is the stub as built; the step-through the mockup drew is item 2 below, its centred panel superseded by the sheet (ADR-052) | `MainPage::DrawPanel` |
| 08 | Missed digests | **Partial.** The backlog is kept and concatenated (ADR-044) and its repeats are folded (ADR-062); no tabs | `08-missed-digests.png` (`SINCE YOU LOOKED - T1 > T7`, the delta box, two actor cards and a folded `PRODUCTION +18 - T1 > T7`); the tabs the mockup drew are item 3 below | `Lockstep/Lockstep.cpp` (match loop), `DigestView.cpp` |
| 09 | Seats — the host's lobby | **Built**, and not in the handoff (ADR-036, 037, 041, 051, 066) | `09-seats.png` (the host alone), `09-seats-joined.png` (a second seat connected and selected, and the three-way on every card) | `Lockstep/SeatsPage.cpp` |

Also built with no mockup, all described in `SCREENS.md` and all captured but one: the four
**sheets** (build, destination, signal, replay — ADR-052), the **WAITING FOR THE HOST** and
**REFUSED · NOT UNDERSTOOD** dialogs (ADR-038), and the main page's **finished-match** state. The
one not captured is REFUSED · NOT UNDERSTOOD, which needs a malformed hello that no client sends.

**Two states of the build have no capture and cannot get one from a practice match**, which is worth
saying rather than leaving a reader to look for them. The digest's **page band** (ADR-061) needs a
card stack taller than a 648-pixel column, and six players over eight ticks produce three cards;
`DigestOverflowTapTests` builds a twenty-card digest and presses the band instead. The destination
row's **garrison line** (`P3 - 11 +DEF`, ADR-063) needs a rival fleet parked on a neighbour of the
fleet being moved, which the opening board never has — `01-destination-sheet.png` is the ordinary
case, every candidate `UNCLAIMED`.

## Flow

```
Lockstep.exe ──► 03 Join ──JOIN──► host:   09 Seats ──ENTER MATCH / PRACTICE MATCH──► 01 Main page
                              └──► joiner: 01 Main page, under WAITING FOR THE HOST until the host enters
--join host --token x   skips 03; a refusal or an unreachable host lands on 03 with the 05 dialog over it
--serve                 draws nothing (a console, ADR-044)

01 ──countdown reaches zero──► 06 ──next state arrives──► 01
01 ──link drops──► 04 over 01 ──reconnected──► 01 (08's SINCE YOU LOOKED if ≥2 ticks passed)
01 ──match ends──► 05 MATCH FINISHED ──VIEW LAST DIGEST──► 01 in its finished state
```

The join and seats screens run in their own frame loops (`RunJoinScreen`, `RunSeatsScreen`); the
match loop draws the main page and the dialog over it, and redraws only when something changed
(ADR-047) or a fleet is under way (ADR-055).

## What the design asks for that the tree does not have

The complete list, so nobody goes looking. Each item is also under its screen in `SCREENS.md`.

1. **Screen 02, entirely.** No `SHARE TICK` button on the top bar, no card, no clipboard.
2. **Screen 07's step-through.** `REPLAY T<n> - NOT YET WIRED` opens a sheet naming six phases and
   nothing else: no per-phase entries, no PREV/NEXT, no board snapshot per phase. The snapshot
   carries no `PhaseRecord`s, so this needs the wire before it needs a screen.
3. **Screen 08's tabs**, per-tick event counts, the collapsed older ticks and read marks. What
   exists is the data (eight ticks per player, ADR-044) shown as one concatenated list under
   `SINCE YOU LOOKED`. A restarted client cannot know what it has read — R13 leaves it nothing to
   write — so it opens with the whole backlog as a plain `DIGEST - TICK n`.
4. **Screen 05's `WELCOME · SEAT n OF 12` dialog.** A welcome goes straight to the match; the seat
   is said on the join screen's seat line and the missed ticks by the digest header. Two dialogs the
   design did not have exist instead (WAITING FOR THE HOST, REFUSED · NOT UNDERSTOOD).
5. **On the main page:** an event card has no highlighted variant and no tick stamp on the right
   (`T45`); nothing in the client scrolls (ADR-052 option C) — a digest too tall for its column
   collapses its actor cards and then pages, which is ADR-061 rather than a scroll. The locks rail's
   rows became links on 2026-09-12 (ADR-060), except the `SIGNALS` section's.
6. **On the map:** nothing marks a system that is building — a rising build is a row on the
   rail, the sheet and the digest and not a mark on the node (ADR-070, whose open question is
   whether that is enough of a tell); owner tags beside names (`NARTH · OKO`), the contact
   spotlight, a verdict label
   under a node (`T47 · YOU LOSE`), the rival's approach lane in amber, a proposed lane in amber
   (built: blue dashed), focus by actor (built: by system), and the Fallow's whole distinct
   treatment — dithered ground, radial glow, three stepped rings, five pins, its name and the race
   distances. Built: a dashed purple ground circle, one lifted ring, three pins and
   `SEALED - OPENS T<n>`. Zoom: `PointerInput` banks wheel notches and pinches, and nothing reads
   them; `MainPage::ResetView` exists and no control reaches it.
7. **The destination sheet's verdict.** A row says what is standing on a candidate (`P3 - 11 +DEF`,
   ADR-063) and not how the fight would go. `YOU WIN` / `HOLD` / `YOU LOSE` per candidate needs a
   preview per candidate on `SnapshotFleet`; the combat parameters are not on the wire and the
   client must not learn the rule (`GameLogic/Snapshot.h`).
8. **The trade lane as a `PROPOSE` row under BUILDS.** `BuildRow::isTradeLane` is set by nothing,
   so the amber row never appears; a lane offer is made from the signal picker instead (ADR-039's
   open question).
9. **Twelve seats** on the seats screen. Six are drawn, every one required (ADR-036 amendment 3).

Found while reading the code for this record, not design gaps: the top bar's
`M<id>` is the tick number zero-padded (`SnapshotView.cpp`, the snapshot carries no match id) and
`D<n>/21` assumes four ticks a day and twenty-one days (ADR-051 records it); and
`JoinPage::SetMatchSummary` has no caller, so the join screen's footer line never appears.
`Design/GETTING-STARTED.md` said rail rows jump to their event, that the map zooms, that a player
starts with twenty credits and that missed digests are one tick deep; none was true of the tree, and
it was corrected on 2026-09-12. (Rail rows became links later the same day — ADR-060 — so that first
claim is true again, of a different mechanism: a row links to the thing it names, not to an event.)

**Every capture is current as of 2026-09-13.** The five ADR-069 and ADR-070 had left stale were
retaken that day from the Debug build at 9b25816, on an unlocked desktop, along with every other
state those changes reach: eighteen retaken, sixteen of them different, and `01-build-rising.png`
staged for the first time. The two that came back byte for byte what they were are
`06-at-lock.png` and `05-waiting-for-the-host.png` — a locked tick-zero rail and a joiner with no
board yet, which neither change reaches — and that is the check. `03-join.png`,
`05-connecting.png` and the two refusals were left alone: nothing has touched `JoinPage` or
`ConnectionDialog` since they were taken.

## Photographing the build

Done on 2026-09-12: every screen and state above was captured from the Debug build of the tree at
a4c9235, and the eight mockups left `screens/` the same day — the four the build had superseded and
the four whose targets the list above keeps in words. Every capture the eight changes of ADR-060 to
ADR-066 touched was retaken the same day from the Debug build carrying them. Done again on
2026-09-13 for ADR-069 and ADR-070, by the same recipe. What is known about doing it, measured on
2026-09-12 and confirmed on 2026-09-13:

- `Build/Screenshot.ps1 -Exe x64\Debug\Lockstep.exe -Out shot.png -Arguments "--tick 4 --store scratch"`
  captures the client area (DPI-aware, cropped to the 1280×720) and gets you screen 03 as it opens.
  `--store scratch` keeps the run off the player's own store; delete `x64\Debug\scratch.*` after.
- **Taps need `SendInput` on an unlocked desktop** — `Build/TapRehearsal.ps1`'s `TapBuild` class.
  The client takes input through the pointer API, and `WM_POINTER*` cannot be posted (`PostMessage`
  fails with error 1002). A posted `WM_KEYDOWN` *does* land, locked or not: Enter is JOIN on
  screen 03 and ENTER MATCH on 09. `Get-Process LogonUI` says whether the desktop is locked; if it
  is, the window will report itself foreground and no tap will happen.
- Client-pixel targets: JOIN (826, 479); PRACTICE MATCH (744, 482); `REPLAY T<n>` (1210, 24); a
  sheet's `CANCEL` bar (700, 688); the `SIGNALS` header on a fresh rail (1150, 201). The filled
  build button moves with the digest — scan column x=40 for solid blue, as `TapBuild` does. **The
  `SIGNALS` header moves too**: it sits under BUILDS, so a queued build or a second fleet pushes it
  down 16 pixels a line, and 201 is only right on an opening rail. Since ADR-069 put the level in
  the title a queued build wraps onto a second line and costs 28, not 16: measured at 229 with one
  build queued and 257 with two on 2026-09-13, where a tap at 201 silently opened that build's
  sheet instead and photographed the wrong screen.
- **The rail's hover is read back, not eyeballed** (ADR-060). `HOVER_FILL` is white at 20/255 over
  the ink, which lands on `30,33,38` against a background of `11,14,20` — plain in place and easy to
  miss in a thumbnail, so a capture script should assert the pixel. Measured that way on 2026-09-12,
  and it is what found that a moving mouse produces no `WM_POINTERUPDATE` at all.
- `01-build-rising.png` is a practice match one tick after queuing a level: PRACTICE MATCH, tap a
  held system, queue the top row, let one lock pass. The rail then carries the rising row and the
  same system's sheet offers nothing (ADR-070). Tap the system again before photographing it, so
  the sheet, the rail row and the digest's card are all in the one frame.
- States: 09 = JOIN as the host; 01 = PRACTICE MATCH (`--tick 4` overrides the preset's two
  minutes); the sheets = a held system on the map, `MOVE`/`REDIRECT`, the `SIGNALS` header, `REPLAY`;
  06 = a `--serve` process suspended past a lock (`NtSuspendProcess`); 04 = that process killed;
  08 = it restarted after two or more ticks (it resumes, ADR-042) with the client still running;
  05's refusals = a second client with the same token (SEAT IN USE) or a wrong one; CONNECTING =
  `--join 192.0.2.1:7341`; MATCH FINISHED = `--serve --phase0 --tick 2 --bots 5` and a client as
  `alpha`, about a hundred seconds; WAITING FOR THE HOST = a joiner into a lobby whose host is still
  on 09 (its token is on the host's screen or on the clipboard after `COPY`).
- **A parked fleet has no marker** (`MapRender.cpp` draws a fleet only while it moves), so the
  destination sheet opens only from a `MOVE FLT n` standing move — tick zero, or any tick on which
  no card offers a control (ADR-056) — or from `REDIRECT`. On `--tick 60` there is time to open it,
  read the rows and pick one; a sheet's rows sit 44px apart up from the `CANCEL` bar, the last at
  y≈645.
- **06, 04 and 08 from one dedicated server:** `--serve 7351 --phase0 --tick 30 --bots 5 --store
  <name>` and a client `--join 127.0.0.1:7351 --token alpha`. `NtSuspendProcess` the server and
  wait for the client's countdown to pass zero (06); open the `SIGNALS` sheet first and suspend it
  again for 06 with a sheet across the lock (ADR-065); resume it and it catches up; kill it mid-tick
  (04).
- **08 needs the CLIENT to be the one that was away**, which is not what the line above used to say
  and cost a run to find out on 2026-09-12. The whole backlog is sent on ARRIVAL and the newest tick
  on every tick after (ADR-044, `MatchServer::PushState`), so a client that merely watched a
  restarted server catch up has one digest and no gap at all, and one that was connected throughout
  a suspension gets the gap with a single event in it. What produces `SINCE YOU LOOKED - T1 > T7`
  over several ticks of events is: suspend the CLIENT, kill the server under it, restart the server
  with the same `--store` and let it run five or six ticks, then resume the client — its socket is
  dead, it reconnects, and a reconnect is an arrival with a `drawnTick` several ticks old.
  `--tick 20` makes that about two minutes.
- **MATCH FINISHED** from a practice match on `--tick 4`, which ends in two minutes; `VIEW LAST
  DIGEST` is the filled button at about (806, 403). **WAITING FOR THE HOST:** on 09 tap seat 02's
  card (500, 230) and its `COPY` (1080, 274), then start a joiner with the clipboard's token.
- The helper that took these — `PrintWindow` capture, `SendInput` taps, a posted Enter,
  suspend/resume — was `Build/TapRehearsal.ps1`'s class extended in a scratch script; it is not in
  the tree.

## Non-negotiables
1280×720 logical, drawn 1:1 (ADR-011). One 8×8 bitmap font at 1×, and 2× only where the
guidelines say. 8-bit RGBA colours from `LockstepClient/DesignTokens.h`. No anti-aliasing; text on
integer pixels.
