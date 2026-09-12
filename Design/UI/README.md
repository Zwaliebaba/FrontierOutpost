# LockStep: Universe — the client's screens

What the client shows, screen by screen, and how far each screen is built. The design arrived on
2026-09-11 as a UI handoff — eight mockups at 1280×720, guidelines, a per-screen spec and a work
plan ("v2", superseding the v1 `Design/Screens/`). Most of it has since been built, some of it
deliberately not, and this directory is now the **design record for the client**, kept against the
code in `LockstepClient/` and `Lockstep/`. Every status below is as of **2026-09-12** and was read
from the tree, not from the plan.

## Contents
- `SCREENS.md` — per screen: what is built, what the design asked for that is not, and where the code is.
- `DESIGN-GUIDELINES.md` — frame, font, palette, components, copy and map rules **as built**, with the design's intent kept where the build stops short of it.
- `screens/` — the eight mockups of 2026-09-11, PNG at 1×, 1280×720 unless noted. **None of them is a photograph of the build.** The table below says which still describe a target and which the build has superseded; replacing the superseded ones with captures of the running client is owed and not done here.
- The handoff's work plan, `PROMPT.md`, is finished as far as it is going to be and lives in [`Design/Archive/2026-09-11-ui-v2-prompt.md`](../Archive/2026-09-11-ui-v2-prompt.md) with a note per step. ADR-034, ADR-038 and ADR-039 cite it by its old name.
- Live design reference: the project file `Frontier Outpost Main Page.dc.html` (all mockups on one canvas; design reference, not production code). **The filename predates the rename to LockStep: Universe and is deliberately left alone** — the file lives outside this repository, so renaming it here would break the pointer without renaming anything (ADR-035).

Built from `Design/space-4x-one-pager-v10.md`, ADR-014 (interface layer), ADR-027 (owner colours),
ADR-028/029 (roles, tokens) and the decisions since: ADR-034 (what the handoff left open) and
ADR-036 through ADR-059, which are cited where they apply.

## Screens

| # | Screen | Status | Mockup in `screens/` | Code |
|---|---|---|---|---|
| 01 | Main page — digest as order surface, map, locks rail | **Built** (ADR-034; then 045, 052, 053, 055–059) | `01-main-page-5a.png` — still the reference for the map's unbuilt details | `LockstepClient/MainPage.cpp`, `DigestView.cpp`, `MapRender.cpp` |
| 02 | Share tick — 480×640 export | **Not built.** No button, no export; ADR-034 §2 says clipboard-only if it is ever built, and its open question asks whether it should be | `02-share-tick-5b.png` — the target, if there is one | — |
| 03 | Join | **Built** (ADR-034 §3, ADR-041) | `03-join-3a.png` — superseded by the build | `LockstepClient/JoinPage.cpp`, `NeuronClient/TextField.h` |
| 04 | Connection lost | **Built** (ADR-038, ADR-043) | `04-connection-lost-3b.png` — superseded; drawn on the v1 page | `LockstepClient/ConnectionDialog.cpp` |
| 05 | Connection states | **Built** as one component with seven states. The WELCOME dialog is not one of them | `05-connection-states-3b.png` — superseded | `LockstepClient/ConnectionDialog.cpp` |
| 06 | At lock | **Built** (ADR-039) | `06-orders-locked-3c.png` — superseded; v1 layout | `LockstepClient/MainPage.cpp` |
| 07 | Replay — phase step-through | **Stub.** A sheet listing the six phases; nothing steps | `07-replay-3d.png` — the content is still the target; the centred panel it draws is superseded by the sheet (ADR-052) | `MainPage::DrawPanel` |
| 08 | Missed digests | **Partial.** The backlog is kept and concatenated (ADR-044); no tabs | `08-missed-digests-3e.png` — the tabs are still the target; v1 layout | `Lockstep/Lockstep.cpp` (match loop), `DigestView.cpp` |
| 09 | Seats — the host's lobby | **Built**, and not in the handoff (ADR-036, 037, 041, 051) | none | `Lockstep/SeatsPage.cpp` |

Also built with no mockup, all described in `SCREENS.md`: the four **sheets** (build, destination,
signal, replay — ADR-052), the **WAITING FOR THE HOST** and **REFUSED · NOT UNDERSTOOD** dialogs
(ADR-038), and the main page's **finished-match** state.

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
2. **Screen 07's step-through.** `REPLAY T<n>` opens a sheet naming six phases and nothing else: no
   per-phase entries, no PREV/NEXT, no board snapshot per phase. The snapshot carries no
   `PhaseRecord`s, so this needs the wire before it needs a screen.
3. **Screen 08's tabs**, per-tick event counts, the collapsed older ticks and read marks. What
   exists is the data (eight ticks per player, ADR-044) shown as one concatenated list under
   `SINCE YOU LOOKED`. A restarted client cannot know what it has read — R13 leaves it nothing to
   write — so it opens with the whole backlog as a plain `DIGEST - TICK n`.
4. **Screen 05's `WELCOME · SEAT n OF 12` dialog.** A welcome goes straight to the match; the seat
   is said on the join screen's seat line and the missed ticks by the digest header. Two dialogs the
   design did not have exist instead (WAITING FOR THE HOST, REFUSED · NOT UNDERSTOOD).
5. **On the main page:** the locks rail's rows are not links (tapping one does nothing; only the
   `SIGNALS` header is a control); an event card has no highlighted variant and no tick stamp on the
   right (`T45`); an actor card does not collapse; the digest does not scroll — cards past the bottom
   of the screen are cut, and nothing in the client scrolls (ADR-052 option C).
6. **On the map:** owner tags beside names (`NARTH · OKO`), the contact spotlight, a verdict label
   under a node (`T47 · YOU LOSE`), the rival's approach lane in amber, a proposed lane in amber
   (built: blue dashed), focus by actor (built: by system), and the Fallow's whole distinct
   treatment — dithered ground, radial glow, three stepped rings, five pins, its name and the race
   distances. Built: a dashed purple ground circle, one lifted ring, three pins and
   `SEALED - OPENS T<n>`. Zoom: `PointerInput` banks wheel notches and pinches, and nothing reads
   them; `MainPage::ResetView` exists and no control reaches it.
7. **The trade lane as a `PROPOSE` row under BUILDS.** `BuildRow::isTradeLane` is set by nothing,
   so the amber row never appears; a lane offer is made from the signal picker instead (ADR-039's
   open question).
8. **Twelve seats** on the seats screen. Six are drawn, every one required (ADR-036 amendment 3).

Found while reading the code for this record, not design gaps: the replay stub's own note row
(`NOT YET WIRED TO A RESOLVED TICK`) is the seventh row of a six-row sheet and is clipped, so the
sheet reads `+1 MORE THAN THIS SHEET CAN SHOW` instead of saying it is a stub; the top bar's
`M<id>` is the tick number zero-padded (`SnapshotView.cpp`, the snapshot carries no match id) and
`D<n>/21` assumes four ticks a day and twenty-one days (ADR-051 records it); and
`JoinPage::SetMatchSummary` has no caller, so the join screen's footer line never appears.
`Design/GETTING-STARTED.md` §2 says rail rows jump to their event and that the map zooms, and §3
says twenty starting credits — none of the three is true of the tree (ADR-055 made it a hundred).

## Photographing the build

The mockups in `screens/` should be replaced by captures of the running client, screen by screen.
What is known about doing that, measured on 2026-09-12:

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
  build button moves with the digest — scan column x=40 for solid blue, as `TapBuild` does.
- States: 09 = JOIN as the host; 01 = PRACTICE MATCH (`--tick 4` overrides the preset's two
  minutes); the sheets = a held system on the map, `MOVE`/`REDIRECT`, the `SIGNALS` header, `REPLAY`;
  06 = a `--serve` process suspended past a lock (`NtSuspendProcess`); 04 = that process killed;
  08 = it restarted after two or more ticks (it resumes, ADR-042) with the client still running;
  05's refusals = a second client with the same token (SEAT IN USE) or a wrong one; CONNECTING =
  `--join 192.0.2.1:7341`; MATCH FINISHED = `--serve --phase0 --tick 2 --bots 5` and a client as
  `alpha`, about a hundred seconds; WAITING FOR THE HOST = a joiner into a lobby whose host is still
  on 09 (its token is on the host's screen or on the clipboard after `COPY`).

## Non-negotiables
1280×720 logical, drawn 1:1 (ADR-011). One 8×8 bitmap font at 1×, and 2× only where the
guidelines say. 8-bit RGBA colours from `LockstepClient/DesignTokens.h`. No anti-aliasing; text on
integer pixels.
