# Prompt for Claude Code — integrate the Frontier Outpost main page

Paste the text below into Claude Code from the repo root (`design_handoff_main_page/` should be committed alongside `Design/`).

---

Implement the Frontier Outpost **main page** from the design handoff in `design_handoff_main_page/`.

Read first, in this order:
1. `Design/space-4x-one-pager-v10.md` — the game rules the screen expresses (tick lock, digest, three-column orders, trade lane as a building, custodian, sealed region, public score).
2. `design_handoff_main_page/README.md` — the full screen spec: layout grid, tokens, copy, map projection, interactions, state model.
3. Open `design_handoff_main_page/main-page-pixelfont.html` in a browser to see the target. It is a **design reference in HTML, not code to copy** — recreate it in this codebase's UI stack and conventions.

Constraints:
- Fixed 1280×720 logical resolution. Colours are 8-bit RGBA exactly as listed in the README tokens.
- The project has one font: the 8×8 fixed bitmap font already in the codebase. Use it at 1× (8px) everywhere and 2× (16px) only for the lock countdown (top bar and orders footer). No other sizes, no anti-aliasing, text origins on integer pixels. Where the reference shows a proportional font, treat it as spacing/hierarchy guidance only.
- Layout: top bar 48px; below it three columns 300 | fill | 330 (digest rail · map · orders rail), 1px `rgba(255,255,255,0.10)` separators.
- Map: render the galaxy graph on a tilted ground plane using the projection in the README (`d = y/560; s = 0.5+0.65d; sx = 400+(x−400)s; sy = 60+440(0.3d+0.7d²)`), letterboxed to the pane (never cropped). Lanes and the sealed region lie on the plane; systems rise on stems with ground shadows; fleets in transit hover above their lane at progress fraction with a tick-ETA label. Node/stem/label scale with `s`.
- Wire the screen to real state via the model in the README's **State** section (match, player, digest, graph, fleets, orders, proposals, region). Use the existing tick/graph code if present; stub a fixture matching the reference data (tick 46 → 47, 12 players, the seven digest events, two fleets, three build rows incl. the greyed trade-lane *Propose* row, one open proposal) so the screen renders identically to the reference on first run.
- Behaviour to implement now: live countdown to `nextLockAt`; orders editable until lock and locking together; Accept/Decline on a proposal is itself an order; digest event → focus map node; system tap → build list; fleet tap → lane-constrained destination picker with tick ETA; Replay tick N opens the last resolved tick's phase step-through (can be a stub view).
- Do not add anything the one-pager excludes: no chat/free text, no notifications per event, no diplomacy tab.

Deliver: the screen wired to the fixture, plus a short note listing anything in the reference you could not reproduce with the bitmap font at 8px (overflowing copy, etc.) with your proposed shortened copy.
