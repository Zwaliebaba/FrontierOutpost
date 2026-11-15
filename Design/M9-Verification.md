# M9 delivery and verification

Plan M9 (station exterior and interior) is implemented: the station is two point two times its
old size, solid, and docked through one hangar mouth; its facilities show as tells outside and
as modules inside; the berths are gone. Three things remain the owner's: the 9.1 greybox runs
that confirm or change the scale, the 9.7 measurements, and the checkpoint below.

## Owner decisions this build rests on (2026-11-15)

- The hull is solid, with the mouth as the only way in.
- `frontier-sim` owns the station's gameplay geometry (`crates/sim/src/station.rs`); the art
  pipeline checks the exterior against it.
- The mouth faces the site's centre, the direction ships launch.
- Launch is a scripted exit along the lane in roster order, then control at the lane's end.
- Docking is entering a capture zone in front of the mouth, moving inward; the rules then draw
  the ship in.
- Socket B's adjacency is "beside the hangar deck".
- The build uses the art's 2.2× scale; 9.1's greybox may change the constants and the art.
- M7 is measured later, on the hangar build (below).

## Items

| Item | Delivered proof |
| --- | --- |
| 9.1 | The readability greybox has a ring stand-in at scales 1 (the old station), 2.2 and 3, with four mouth facings, hangar traffic, and results named per setting; the report gives gate 2 per station setting and vision mode, never pooled. Tests in `client/src/greybox/*.test.ts` and `crates/server/src/report.rs`. **The owner's greybox runs, and the scale choice, are open.** |
| 9.2, 9.3 | The exterior and interior (`Design/Art/station/`, from the art study merged before M9) pass `check`, including the new rule that the mouth, its opening, the lane and the footprint match `station.rs`; every facility's tells exist from M8. |
| 9.4 | `station.rs` (hull as circle obstacles leaving the lane open, mouth, lane, capture zone); scripted departures (8 m/s, 10 m apart, roster order, wingmen behind their pilot) with `Undocked` at the lane's end; capture-zone docking with a 6 m/s draw-in; jumps home measured from the mouth and never on the footprint (72 directions × 6 distances, both sites); asteroids clear of the footprint and lane; roster slot replaces the berth slot, old checkpoints still load. Sim tests include four ships launching without colliding, a jump home landing clear, and a launch passing crew ships coasting beyond the lane's end. |
| 9.5 | Protocol 15: `SiteInfo.station` carries the geometry and hull obstacles; `CrewMember.berth` and `dock_radius` are gone; `RunPhase::Docking`. Integration test `four_pilots_launch_from_the_hangar_in_turn_and_dock_through_its_mouth` (`crates/server/tests/crew.rs`). |
| 9.6 | The client draws the exterior (with tells on their hardpoints) at home and at the dock, turned by the rules; selecting the station opens the interior in the same camera, where building and upgrading happen; ships are hidden inside the hangar and seen leaving and entering the mouth; the camera holds on the mouth during a departure; the HUD and map speak of the hangar, not berths; landing gear stays folded in flight. Browser tests: open the interior, build in a socket and see its tell outside, launch and dock through the mouth (`client/e2e/outpost.e2e.ts`, `hangar.e2e.ts`), four pilots handed over at the lane's end (`crew.e2e.ts`). |
| 9.7 | **Open: the owner's measurement.** |

The legacy station, its berths and `berths.json` are retired: `create_scene.py` no longer builds
them (the asteroids regenerate byte-identically), and the catalogue's legacy list holds only the
asteroids.

## Validation

Run on the final tree with the pinned Rust toolchain, Node 26 and Blender 4.3.0 (as a Python
module):

- `cargo fmt --all --check`, `cargo clippy --workspace --all-targets --locked -- -D warnings`,
  `cargo clippy -p frontier-sim-wasm --target wasm32-unknown-unknown --locked -- -D warnings`
- `cargo test --workspace --locked`: 486 passing tests; the generated protocol files unchanged
- `cd client && npm run check`: 435 passing tests, format, lint, types and build
- `npx playwright test` (all browser tests against real servers; Chromium preinstalled in the
  build container, not the pinned build): 49 passing tests, none failing
- `blender ... test_frontier_art.py`: 23 passing tests; `test_check_exports.py`: 16 passing
- `python3 Design/Art/tools/check_exports.py` and `python3 Design/Art/tools/catalogue.py --check`

## Owner checkpoint before M10

Build a facility inside, see its tell outside, launch out of the mouth and dock back through
it. The plan's test: if the exterior hides ships or crowds the dock, or the interior does not
read as the outpost changing, rework it before M10 builds on it. Then run the greybox block at
each scale and facing in normal, grey and one colour-deficient mode, and confirm the 2.2× scale
or choose another (the constants in `station.rs` and the exterior change together; the art
check keeps them in step).

## Gates and M7

9.7 re-measures gates 2 and 4 on this station. M7 was not closed before M9 (the owner chose to
proceed), so Stage 1's berth-era gate 4 figures were never taken and cannot be now: undocking
and docking are measured from the mouth since 9.4. M7's write-up and 9.7's addendum must say
so, so that no figure is read as a like-for-like Stage 1 measurement.

## Known limits

- A crew ship stopped on the lane, or touching its end, holds later departures until it moves;
  in a crew this could be used to block others.
- Tells are small on the 32 m ring (about 25 px at 1080p at the home framing).
- Runtime hangar lighting is not implemented.
- Blender 5.x has not been run on the pipeline here.
