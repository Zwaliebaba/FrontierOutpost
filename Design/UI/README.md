# LockStep: Universe — UI handoff v2

Screens for the tablet/desktop client, 1280×720, single 8×8 bitmap font. Built from `Design/space-4x-one-pager-v10.md`, ADR-014 (interface layer), ADR-027 (owner colours), ADR-028/029 (roles, tokens), `NeuronCore/Protocol.h`, `GameLogic/TickLog.h`. Supersedes `Design/Screens/` (v1 main page).

## Contents
- `screens/` — PNG at 1× (pixel-exact, 1280×720 unless noted)
- `DESIGN-GUIDELINES.md` — frame, palette, type, components, copy rules, map projection
- `SCREENS.md` — per-screen spec: anatomy, state, behaviour, what code it maps to
- `PROMPT.md` — paste into Claude Code to integrate
- Live reference: the project file `Frontier Outpost Main Page.dc.html` (all screens on one canvas; design reference, not production code). **The filename predates the rename to LockStep: Universe and is deliberately left alone** — the file lives outside this repository, so renaming it here would break the pointer without renaming anything.

## Screens
| # | File | Screen | Replaces / adds |
|---|---|---|---|
| 01 | `01-main-page-5a.png` | Main page — digest as order surface | replaces `MainPage.cpp` layout |
| 02 | `02-share-tick-5b.png` | Share tick — 480×640 portrait export | new |
| 03 | `03-join-3a.png` | Join / seat | new (replaces `--join` CLI only) |
| 04 | `04-connection-lost-3b.png` | Connection lost overlay | new |
| 05 | `05-connection-states-3b.png` | Connecting · Refused ×2 · Finished · Welcome dialogs (1280×366 strip) | new |
| 06 | `06-orders-locked-3c.png` | Main page at lock | new state of 01 |
| 07 | `07-replay-3d.png` | Replay tick N — phase step-through | replaces the phase-list stub |
| 08 | `08-missed-digests-3e.png` | Missed digests — 3 ticks waiting | new state of 01 |

Screens 06–08 were drawn on the v1 three-rail main page; the layout to implement is 01. Their *states* (locked rail, replay panel, unread-tick tabs) apply unchanged to 01 — see `SCREENS.md`.

## Non-negotiables
1280×720 logical. One 8×8 bitmap font at 1× and 2× only. 8-bit RGBA colours as listed. No anti-aliasing; text on integer pixels.
