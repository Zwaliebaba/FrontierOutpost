
Read, in order:
1. `Design/space-4x-one-pager-v10.md` — the rules the screens express.
2. `Design/ADR/ADR-014-*.md` (interface layer, 8×8 font), `ADR-027` (owner colours), `ADR-028`, `ADR-029` (roles, tokens).
3. `Design/UI/README.md`, `DESIGN-GUIDELINES.md`, `SCREENS.md`.
4. Open the PNGs in `Design/UI/screens/`. They are design references, not code to port.
5. The current implementation: `Lockstep/MainPage.*`, `GameLogic/TickLog.h`, `NeuronCore/Protocol.h`.

**Four of these were put to the owner on 2026-09-11 and answered — see ADR-034.**

1. **The map keeps its orbit camera.** The projection formula in the guidelines describes how the
   mockups were rendered; implement against `Neuron::OrbitCamera` (ADR-017), which the sky depends
   on (ADR-032).
2. **The share card goes to the clipboard only, never to a file.** A client that writes a file would
   be R13's first client-side exception and it is not worth one.
3. **The join screen gets a real text field**, added to the interface layer. The product is a mobile
   client (blueprint §7) and a phone has no command line.
4. **No reference fixture.** The "Fixture" paragraph below is superseded: screens render what the
   server sends. `--tick <seconds>` is how you reach a state that exercises a screen.

Constraints (non-negotiable):
- 1280×720 logical, letterboxed. The existing 8×8 bitmap font at 1× everywhere and 2× only for the lock countdown and the share-card headline. No anti-aliasing; integer pixel origins.
- Colours exactly as listed in `DESIGN-GUIDELINES.md` (8-bit RGBA). Owner colours from ADR-027; you are always blue.
- Dedicated-server model: no host-left state. Refusal dialogs map 1:1 to `RefusalReason`.

Work plan:
1. **Main page (01).** Replace the three-rail layout with `400 | fill | 260`: digest as order surface (events carry their actions), map, read-only locks list. Implement the digest presentation layer over `TickLog` events: consequence ranking (system lost > contact next tick > proposal > custodian > region > income > silence), actor grouping (≥2 events by one player → one card ranked by the worst, `LEADER` tag when applicable), and the verdict string from the combat preview (`YOU LOSE · You arrive N. He holds M +def. X of his remain, Y of yours.`). `SINCE YOU LOOKED` header and four-cell delta when unread ticks ≥ 1. System/player counts from match data.
2. **Map.** Apply the projection and rendering rules in the guidelines; name every node with owner tag; contact spotlight; fleets in transit with tick labels; make the Fallow the most distinct object (dithered ground, glow, stepped rings, site pins, race distances). Map focus follows digest `MAP` actions.
3. **Lock state (06).** At countdown zero flip header/rail to LOCKED, dim and disable all actions, show the resolving notice; recover on new `TickLog`.
4. **Replay (07).** Replace the phase-list stub with the step-through panel reading `TickLog.phases`; render the board snapshot for the current phase behind it.
5. **Missed digests (08).** Tabs per unread tick, latest expanded, older collapsed. If the server keeps only the latest digest, implement the UI one deep and open an ADR to retain N digests.
6. **Join + connection (03, 04, 05).** Join screen with server/token (add a minimal text field to the interface layer, or keep CLI args and use the screen as confirmation — state which). Connecting / Refused ×2 / Finished / Welcome dialogs; connection-lost overlay with retry loop, local order retention and re-send, countdown still live.
7. **Share tick (02).** Export a 480×640 PNG of the top event: map crop + headline + delta + standings. Local file / clipboard only.

~~Fixture: seed a match matching the reference (tick 46 → 47, 12 players, 61 systems, the six digest items, Halvorsen leader 1,610, you 4th 1,284, Fallow opens T60) so every screen renders like its PNG on first run.~~ **Superseded by ADR-034:** no fixture. `MatchFixture` was retired by 4X-02 step 2 and stays retired; a second source of truth about what a match looks like drifts from the resolver the moment a rule changes. Reach a state that exercises a screen by playing or replaying to it.

Deliver: the screens wired to the fixture, a note per screen on anything the 8px font could not fit (with proposed shortened copy), and a list of protocol/server changes you needed (digest retention, seat preview before Welcome, finished-match standings).
