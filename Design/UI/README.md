# LockStep: Universe — the client's screens

What the client shows, screen by screen, and how far each screen is built. The design arrived on
2026-09-11 as a UI handoff — eight mockups at 1280×720, guidelines, a per-screen spec and a work
plan ("v2", superseding the v1 `Design/Screens/`). Most of it has since been built, some of it
deliberately not, and this directory is now the **design record for the client**, kept against the
code in `LockstepClient/` and `Lockstep/`. Every status below is as of **2026-09-14** and was read
from the tree, not from the plan.

> **THIRTEEN OF THE TWENTY-THREE CAPTURES ARE STALE AS OF 2026-09-14, AND FIVE MORE ARE OWED.**
> ADR-111 through ADR-114 rebuilt the main page's controls, its sheet, its rail and how a move is
> given, and **not one capture was retaken**: those sessions ran on Linux, and a capture needs a
> Windows D3D12 run of `x64\Debug\Lockstep.exe` through `Build/Screenshot.ps1`. That is a
> statement about where the work happened, not a claim that anything is fine.
>
> Stale: `01-main-page`, `01-orders-queued`, `01-fleet-under-way`, `01-rail-hover`, `01-finished`,
> `01-build-sheet`, `01-build-rising`, `01-destination-sheet`, `01-signal-sheet`,
> `01-signal-sheet-armed`, `06-at-lock`, `06-at-lock-sheet` and `07-replay`. Three of those are of
> a screen that no longer exists at all: the build sheet is a PLACE sheet (ADR-112) and the
> destination picker is a mode on the map (ADR-114).
>
> Owed, and never taken: `01-place-sheet`, `01-move-mode`, `01-move-selected`,
> `06-at-lock-place-sheet`, and a retake of `01-orders-queued` showing the rail's new `ORDERS` and
> `PLACES` sections. `--still` exists for two of them (ADR-114): the move mode's ring pulses and its
> lanes march, so a capture taken without it is of whichever phase the shutter caught.
>
> **The sheet heights in `DESIGN-GUIDELINES.md` §Frame were measured on 2026-09-14 anyway**, off the
> hit rectangles the real screens record rather than off a photograph — the two that close a sheet
> give its top and its bottom, and `PlaceSheetTapTests` asserts the cap they are measured against.
> That is evidence about geometry and none at all about how any of it looks.

> **The paragraph below was true on the morning of 2026-09-14 and is kept because its lesson is.**
> Twelve captures were taken on 2026-09-13 or earlier and nothing since had reached them; **eleven
> were retaken on 2026-09-14** and the reason they all went at once is worth reading, because it is
> the shape of a backlog rather than one change — which is exactly the shape the four ADRs above
> have just made again.
>
> **A capture that needs a FINGER is a capture that stops being retaken.** ADR-103 through ADR-106
> rebuilt how the map draws a system — a lit ball on a stem with a cast shadow, four tones, a rim
> and a glint — over four commits, and each of them retook the same five: `01-main-page`,
> `04-connection-lost`, `06-at-lock`, `08-missed-digests`, `05-match-finished`. Those five are the
> ones a posted `VK_RETURN` can reach. The other eleven need a tap to open a sheet, arm a concede or
> hover a row, a tap needs `SendInput`, and `SendInput` needs an unlocked desktop, which those
> sessions did not have. So eleven pictures went on showing flat discs, shouted card titles
> (ADR-099), 21px rail rows (ADR-100, ADR-101) and, after ADR-107, a build sheet that had stopped
> existing.
>
> Retaken 2026-09-14 from the Debug build at ADR-107, on an unlocked session: `01-build-sheet`,
> `01-build-rising`, `01-destination-sheet`, `01-signal-sheet`, `01-signal-sheet-armed`,
> `01-orders-queued`, `01-fleet-under-way`, `01-rail-hover`, `01-finished`, `06-at-lock-sheet` and
> `07-replay`. **`06-at-lock-sheet.png` is now the BUILD sheet across the lock** rather than the
> signal sheet, because the tile's inert form is the part of ADR-065 that ADR-107 changed and no
> other capture carries it.
>
> **The lesson is a process one and it belongs here:** check `Get-Process LogonUI` at the START of a
> change that touches drawing, not at the end. A locked desktop does not stop the work, it silently
> halves what can be photographed — and the half it takes is the half with the controls in it.
>
> **ADR-108 then made sixteen of them stale again the same day**, which is the fifth time in two days
> the map's lighting has invalidated the same set. All sixteen are retaken. Fifteen changed;
> **`05-match-finished.png` was measured to be unaffected rather than assumed to be** — its scrim
> covers the map entirely and the final standings are deterministic from the seed, so a fresh capture
> hashed identical to the committed one (SHA-256 `a27eb27a…`). That is the check to reach for when a
> capture *might* be reached by a change: hash a fresh one rather than argue about it.
>
> **`01-build-rising.png` improved in the retake and it is worth knowing why.** The 2026-09-14
> version caught a level-TWO build one tick of two in; this one catches a level-THREE build one tick
> of three, so the level ladder shows two pips held rather than one, the progress bar is at a third
> rather than a half, and the digest behind it happens to carry a `NEED 23 MORE` build button —
> a tile state no capture had. Ordering an L3 costs two more locks than an L2 and is worth the wait.
>
> **The check is mechanical rather than visual.** The 8×8 font was one bit a pixel, so a capture
> carrying it has a handful of distinct luminances in it; an anti-aliased one has two hundred. Run
> that over the directory and a stale file cannot hide: it is what caught `05-refused-seat-in-use`,
> which looked retaken in a thumbnail and was not, and `05-connecting`, which had been committed
> **blank** because the capture fired before the window had drawn anything. Neither was visible by
> eye at review size.

## Contents
- `SCREENS.md` — per screen: what is built, what the design asked for that is not, and where the code is.
- `DESIGN-GUIDELINES.md` — frame, font, palette, components, copy and map rules **as built**, with the design's intent kept where the build stops short of it.
- `screens/` — **captures of the running client**, one per screen and state, taken 2026-09-12 from the Debug build of the tree at a4c9235 and retaken 2026-09-13 at 9b25816 wherever the changes since reach them (PNG, 1280×720, the canvas exactly as drawn, captured at `--scale 1`). The eight mockups of 2026-09-11 are no longer in the tree (owner decision, 2026-09-12): the build superseded 03, 04, 05 and 06, and what the other four still asked for — the map's unbuilt details, screen 02, 07's step-through, 08's tabs — is kept in words under "What the design asks for that the tree does not have" and drawn in the live design reference below. The files themselves are in the history up to a4c9235. The table below names each capture.
- The handoff's work plan, `PROMPT.md`, is finished as far as it is going to be and lives in [`Design/Archive/2026-09-11-ui-v2-prompt.md`](../Archive/2026-09-11-ui-v2-prompt.md) with a note per step. ADR-034, ADR-038 and ADR-039 cite it by its old name.
- Live design reference: the project file `Frontier Outpost Main Page.dc.html` (all mockups on one canvas, and since 2026-09-12 the only place they are drawn; design reference, not production code). **The filename predates the rename to LockStep: Universe and is deliberately left alone** — the file lives outside this repository, so renaming it here would break the pointer without renaming anything (ADR-035).

Built from the one-pager (`Design/Archive/space-4x-one-pager-v10.md` since 2026-09-13), ADR-014
(interface layer), ADR-027 (owner colours), ADR-028/029 (roles, tokens) and the decisions since:
ADR-034 (what the handoff left open) and ADR-036 through ADR-071, which are cited where they apply.

## Screens

| # | Screen | Status | In `screens/` | Code |
|---|---|---|---|---|
| 01 | Main page — digest as order surface, map, locks rail | **Built** (ADR-034; then 045, 052, 053, 055–066) | `01-main-page.png` (tick 11 of a six-seat match: a battle, two grouped rivals, production with its priced build, a rising build, a claim — and a paged rail), `01-orders-queued.png` (tick 0: a move ordered and a build queued, before the lock), `01-fleet-under-way.png` (tick 1: the route and its marker), `01-rail-hover.png` (the locks rail with the pointer on a row, ADR-060), the sheets `01-build-sheet.png` (the tile grid, one tile queued, the purse and its sentence — ADR-107), `01-destination-sheet.png`, `01-signal-sheet.png` and `01-signal-sheet-armed.png` (the concede armed under its band, ADR-064), `01-build-rising.png` (a build in flight, one frame carrying all three places it is said: the rail's row, the tile that is rising with its progress bar beside the one it blocks, and the digest's card — ADR-070, ADR-107), and `01-finished.png`. The map's unbuilt details are item 6 below | `LockstepClient/MainPage*.cpp` (one unit per pane), `Controls.cpp`, `DigestView.cpp`, `MapRender.cpp` |
| 02 | Share tick — 480×640 export | **Not built.** No button, no export; ADR-034 §2 says clipboard-only if it is ever built, and its open question asks whether it should be | none; the mockup that was the target, if it stays one, is in the design file and the history | — |
| 03 | Join | **Built** (ADR-034 §3, ADR-041) | `03-join.png` | `LockstepClient/JoinPage.cpp`, `NeuronClient/TextField.h` |
| 04 | Connection lost | **Built** (ADR-038, ADR-043) | `04-connection-lost.png` | `LockstepClient/ConnectionDialog.cpp` |
| 05 | Connection states | **Built** as one component with seven states. The WELCOME dialog is not one of them | `05-connecting.png`, `05-refused-unknown-token.png`, `05-refused-seat-in-use.png`, `05-waiting-for-the-host.png`, `05-match-finished.png` | `LockstepClient/ConnectionDialog.cpp` |
| 06 | At lock | **Built** (ADR-039, ADR-065) | `06-at-lock.png`, `06-at-lock-sheet.png` (the BUILD sheet left open across the lock: every tile keeping its border and its icon with all four of its strings gone `NEUTRAL_DIM`, the filled grey `LOCKED` chip in the header, the rail's sentence in amber — ADR-065, ADR-107) | `LockstepClient/MainPage*.cpp` |
| 07 | Replay — phase step-through | **Stub.** A sheet titled `REPLAY TICK <n>` listing the six phases; nothing steps. Its title said `- NOT YET WIRED` until ADR-091 made `--dev` the disclosure instead, and this line said so until 2026-09-14 | `07-replay.png` is the stub as built; the step-through the mockup drew is item 2 below, its centred panel superseded by the sheet (ADR-052) | `MainPage::DrawPanel` |
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
2. **Screen 07's step-through.** `▶ REPLAY T<n>`, under `--dev` only, opens a sheet naming six phases and
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
   (`T45`). **ADR-052's "nothing scrolls" is down to the three sheets that are columns of rows**:
   the digest pages by cards (ADR-080), the locks rail scrolls in pixels (ADR-101), and the place
   sheet's body scrolls by blocks (ADR-112). The locks rail's rows became links on 2026-09-12
   (ADR-060), except the `SIGNALS` section's — and its `ORDERS` rows gained one cell that is not a
   link, the `X` that takes an order back (ADR-113).
6. **On the map:** nothing marks a system that is building — a rising build is a row on the
   rail, the sheet and the digest and not a mark on the node (ADR-070, whose open question is
   whether that is enough of a tell); owner tags beside names (`NARTH · OKO`), the contact
   spotlight, a verdict label
   under a node (`T47 · YOU LOSE`), the rival's approach lane in amber, a proposed lane in amber
   (built: blue dashed), focus by actor (built: by system), and the Fallow's whole distinct
   treatment — dithered ground, radial glow, three stepped rings, five pins, its name and the race
   distances. Built: a dashed purple ground circle, one lifted ring, three pins and
   `SEALED - OPENS T<n>`. **Zoom, reset and the fleet glyph are built** (ADR-090): a wheel notch or
   pinch step over the map pane moves the camera between 0.6x and 2.5x of the authored framing, a
   `RESET` chip appears beside `MAP - FOCUS` once it has moved, and the legend draws a fleet as the
   arrowhead the map draws rather than as the route's dashes.
7. **The confirm strip's verdict.** A destination row says what is standing on a candidate
   (`P3 - 11 +DEF`, ADR-063) and not how the fight would go. `YOU WIN` / `HOLD` / `YOU LOSE` per
   candidate needs a preview per candidate on `SnapshotFleet`; the combat parameters are not on the
   wire and the client must not learn the rule (`GameLogic/Snapshot.h`).
8. **The trade lane as the place sheet's fourth tile.**
   `BuildRow::isTradeLane` is set by nothing, and since ADR-107 `BuildRow::partner` is beside it and
   is set by nothing either; a lane offer is made from the signal picker instead (ADR-039's open
   question). The tile is styled and tested behind a forced flag, so the day the flag is set the
   first thing anybody finds out is not whether it renders. **The rail's `PROPOSE` row went with the
   `BUILDS` section** (ADR-113): `ORDERS` lists what goes in at the lock, and a lane nobody has
   offered is not that. **The bastion is the same case one step
   further back** — the grid reserves its slot and nothing composes a row for it (blueprint §3,
   after Phase 0) — and so is a tile at its top level, for which `SnapshotView` composes no row at
   all.
9. **Twelve seats** on the seats screen. Six are drawn, every one required (ADR-036 amendment 3).

Found while reading the code for this record, not design gaps: the top bar's
`M<id>` is the tick number zero-padded (`SnapshotView.cpp`, the snapshot carries no match id) and
`D<n>/21` assumes four ticks a day and twenty-one days (ADR-051 records it); and
`JoinPage::SetMatchSummary` had no caller, so the join screen's footer line never appeared — it and
the never-populated seat box are gone (ADR-095).
`Design/GETTING-STARTED.md` said rail rows jump to their event, that the map zooms, that a player
starts with twenty credits and that missed digests are one tick deep; none was true of the tree, and
it was corrected on 2026-09-12. (Rail rows became links later the same day — ADR-060 — so that first
claim is true again, of a different mechanism: a row links to the thing it names, not to an event.)

**Every capture was retaken on 2026-09-13 for FONT-01**, from the Debug build carrying all seven
stages. All twenty-three were different, because the face changed on every screen — which is also
why the by-eye check that served the ADR-069 and ADR-070 retakes did not serve that one, and the
luminance-count check above replaced it. **Eleven of them have since been retaken again**, on
2026-09-14; the block at the top of this file says which and why they had fallen behind.

`01-main-page.png` is **no longer a practice match**. The recipe asked for one and a practice match
at these seeds now yields a digest of one or two events, which documents the screen's typography
poorly; it is a `--serve` match eleven ticks in instead, with thirty-five events, a paged rail and
ten fleets. The practice match is still what the sheet captures are taken from, because it is the
quickest way to a tick-zero board with a `MOVE FLT n` card on it.

**And it is taken from a BOT SEAT, which is the trick that makes a no-tap capture worth looking at**
(2026-09-14). A human seat nobody gives orders for has one fleet and one system for the whole match,
so every board captured without a finger was a board with nothing on it. The server plays seats 2-6
with `--bots 5` and their tokens are the fixed phase-0 list, so `--join <host> --token bravo` puts
the client in front of an empire that has expanded, fought and built -- eleven fleets, a rail deep
enough to page, a digest with thirty events -- without a tap. `01-main-page`, `06-at-lock`,
`08-missed-digests` and `04-connection-lost` are all taken that way, from one server in one run
(scratch `RichShots.ps1`), because a store resumes on whatever tick wall-clock says and two runs are
never at the same one.

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
- Client-pixel targets, **re-measured 2026-09-13 after FONT-01 stage 6 moved two of them**: JOIN
  (826, 479); **PRACTICE MATCH (752, 554)**, which was (744, 482) and then (752, 502) as the seat
  cards grew — re-measured 2026-09-14, and the lesson is that this number has moved three times and
  should be read off a screenshot rather than typed; `REPLAY T<n>` (1210, 24); a sheet's `CANCEL` bar
  (700, 688); the `SIGNALS` header on a fresh rail (1150, 232), which was 201.
- **Do not hardcode a rail position at all — scan for the section dividers.** Each section of the
  locks rail opens with a full-width 1px divider at 7% white, so the third of them down the rail is
  where `SIGNALS` starts and the band is the 22px under it. That is immune to a queued build, a
  second fleet, or the next change to the line height, all of which have broken a literal.
- **A system marker and a fleet marker are the same blue and differ only in size.** Both are exactly
  `94,196,255`; scanning the map pane for runs of it gives system discs at 13–18px wide and a fleet
  at about 9. **Since ADR-112 and ADR-114 a tap on a system opens its PLACE sheet, and a tap on a
  garrison badge with one fleet under it takes that fleet's move onto the map rather than opening
  anything.** A parked fleet is that **badge** beside its system's name (ADR-079) — a 16px filled
  chip, `94,196,255` when it is yours. The digest's filled button moves with the digest — scan
  column x=40 for solid blue, as `TapBuild` does — and since ADR-112 it is a LINK to a place rather
  than an order. **The `SIGNALS` header moves too**: since ADR-113 it sits under `ORDERS` and
  `PLACES`, so a queued build, a second fleet or another held system pushes it down; the rule below
  (scan for the section dividers) is what to use and the numbers above are history.
- **The rail's hover is read back, not eyeballed** (ADR-060). `HOVER_FILL` is white at 20/255 over
  the ink, which lands on `30,33,38` against a background of `11,14,20` — plain in place and easy to
  miss in a thumbnail, so a capture script should assert the pixel. Measured that way on 2026-09-12,
  and it is what found that a moving mouse produces no `WM_POINTERUPDATE` at all.
- **The four captures ADR-111 to ADR-114 owe, and how to take them.** `01-place-sheet.png`: a
  practice match at tick 0, tap the capital, tap one tile — the sheet then shows the BUILD band, a
  queued tile and an available one, the `FLEETS HERE` band and a fleet row with `MOVE ›` on it.
  `01-move-mode.png` and `01-move-selected.png`: the same board with `--still`, tap `MOVE ›`, and
  photograph before and after tapping a lit system — `--still` matters, because the ring pulses and
  the lanes march and two captures without it are two different pictures (ADR-114).
  `06-at-lock-place-sheet.png`: the place sheet open when the countdown reaches zero.
  `01-orders-queued.png` wants retaking for the rail's `ORDERS` and `PLACES` sections either way.
- `01-build-sheet.png` **is of a sheet that no longer exists** (ADR-112); the recipe below is kept
  because the place sheet's build half is the same grid. It is a practice match at tick 0: PRACTICE
  MATCH, tap the capital, tap one tile. That puts `QUEUED −20` / `TAP TO TAKE BACK` on one tile,
  `15 CR` / `85 CR LEFT AFTER` on the other, the purse's ` −20` in the header and the ADR-078
  sentence under it — four of the seven tile states in one frame.
- **`01-build-rising.png` needs a LEVEL TWO build, and that is the trap.** A level-one building
  takes one tick (ADR-069), so it is ordered at one lock and done at the next and is never on the
  screen rising; only L2 and L3 take two and three. The recipe is: `--tick 20`, queue the shipyard
  at tick 0, let it land, queue `SHIPYARD L2` from the digest's filled button, let one more lock
  pass, then tap the system. The sheet then draws `Shipyard L2 rising` with `1 OF 2 TICKS`, its
  progress bar at half, the `RISING · DONE T9` chip in the header and the mining tile beside it dim
  and marked `AFTER T9` — with the rail's row and the digest's card in the same frame (ADR-070,
  ADR-107).
- **Drive it by looking, not by predicting.** The digest re-lays out every tick, so a coordinate
  read from one screenshot is stale by the next: at `--tick 20` a screenshot-then-tap round trip is
  most of a tick. `Build/TapRehearsal.ps1` exists for exactly this — it grabs, scans column x=40 for
  the filled blue button and taps, all in one process between two frames. Anything that has to be
  aimed by eye wants a tick of 45 seconds or more.
- States: 09 = JOIN as the host; 01 = PRACTICE MATCH (`--tick 4` overrides the preset's two
  minutes); the sheets = a held system on the map, `MOVE`/`REDIRECT`, the `SIGNALS` header, `REPLAY`;
  06 = a `--serve` process suspended past a lock (`NtSuspendProcess`); 04 = that process killed;
  08 = it restarted after two or more ticks (it resumes, ADR-042) with the client still running;
  05's refusals = a second client with the same token (SEAT IN USE) or a wrong one; CONNECTING =
  `--join 192.0.2.1:7341`; MATCH FINISHED = `--serve --phase0 --tick 2 --bots 5` and a client as
  `alpha`, about a hundred seconds; WAITING FOR THE HOST = a joiner into a lobby whose host is still
  on 09 (its token is on the host's screen or on the clipboard after `COPY`).
- **A parked fleet draws a garrison badge and not a lane marker** (ADR-079), so the destination
  sheet is opened either from that badge on the map or from the fleet's row in the rail's `FLEETS`
  section (ADR-077); both work on every tick, where the digest's `MOVE FLT n` is there only at tick
  zero and on a tick where no card offers a control (ADR-056). The rail is the easier of the two to
  script: scan for its first section divider and take the 22px band under it rather than a literal
  y, for the reason the `SIGNALS` bullet above gives. On `--tick 60` there is time to open the
  sheet, read the rows and pick one; a sheet's rows sit 44px apart up from the `CANCEL` bar, the
  last at y≈645.
- **A locked desktop blocks taps and not captures.** `SendInput` needs an unlocked session, so
  `TapRehearsal.ps1` and anything built on it stops dead at `LogonUI` being alive — check for that
  process first. `PrintWindow` with `PW_RENDERFULLCONTENT` does not need the foreground, and
  `WM_KEYDOWN` can be POSTED, so a client started as `--join <host> --token alpha` against a
  `--serve --bots 5` server reaches the board on one posted `VK_RETURN` (Enter is JOIN) with no tap
  at all. That is how a board is photographed on a locked machine; it cannot open a sheet.
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
1280×720 logical, drawn into a canvas presented at a whole-number scale and captured **windowed at
`--scale 1`** (ADR-075), which is the one presentation that is exactly the canvas — fullscreen
(ADR-076) letterboxes it and would make a capture the size of whatever monitor took it. Two IBM Plex families in four cuts, baked at 12px and hinted, drawn
**anti-aliased** with coverage gamma-corrected into alpha — mono for data, sans for sentences, and
2× only for the lock countdown and the `LOCKSTEP` title (ADR-073, ADR-074;
`DESIGN-GUIDELINES.md` §Font). 8-bit RGBA colours from `LockstepClient/DesignTokens.h`. A glyph's
coverage is the one thing on this screen a rasterizer decides rather than a designer: `DrawText`
takes whole pixels, and every box, rule and baseline is still on one.
