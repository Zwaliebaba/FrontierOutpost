# M8 art pipeline

The owner directs the art and has authorised agent-authored Blender files in the shared
Blender instance. Sources stay editable. Blender and the pipeline generate binaries and
manifests; the catalogue is assembled by a repository script. Do not edit generated output
by hand.

## Implemented

- `new`, `check` and `export` support facilities, hulls, station exteriors and interiors.
  Blender 4.3.2 generated and verified the committed sources. The tool uses APIs compatible
  with Blender 4.3+ and 5.x; a Blender 5 run has not been performed here.
- Every asset links the shared material library. `contract.json` owns envelopes, budgets,
  tier steps, clearance, material roles, anchors and the tactical view direction
  `(0.43, -0.72, 0.545)` in Blender Z-up space (approximately 33 degrees).
- Evaluated geometry, including modifiers, is checked. Station checks conservatively use
  bounds for hangar/approach clearance, hardpoints, every hull's parking place, and projected
  overlap from the tactical camera. A tight layout may need more space to pass these checks.
  M9 still chooses the station's final physical scale and authors its exterior/interior.
- `split` migrated the six original modules, preserving their evaluated geometry and material
  roles, with reduced signature shapes as tells. The fabrication bay's tier 1 base was lowered
  to Z=0; other geometry was preserved. These assets now live in separate facility sources.
  The frozen scene no longer generates or exports facilities. Its regenerated manifest differs
  only by removing those facility records.
- Crew quarters has two habitat tiers and two matching tells. All four facility families are
  exported, registered in the catalogue and available to the game.
- Facility counts and `art: Art::Ready` / `Art::Pending` come from the literal Rust `STAGE1`
  rules through `game_data.py`; there is no duplicated facility tier list. This reader fails
  if it cannot recognise that format. A future data-file format should replace this bridge.
- CI checks source/tool/library/contract and output hashes, ready-facility coverage, every tier
  and tell, identifiers, names, budgets and catalogue freshness, without needing Blender.
  Pending facilities cannot be built, and are omitted from the game's build catalogue.
- The client finds assets through `catalogue.json`. A facility's GLB is fetched on first use;
  concurrent requests and later tier changes share the parsed templates. Named operating
  meshes remain separately switchable when static parts are merged by material.

The reference station image guides M9's broad ring, heavy hub, segmented armour, warm windows
and illuminated hangar. M8 does not replace the existing station or choose its new dimensions.

## Commands

Run from the repository root, with `blender` on PATH. On Windows, use the full path to
`blender.exe` if needed, and `py` in place of `python3`.

```sh
# One-time shared library creation; refuses to replace an existing library.
blender -b --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- library

# Derives tier count from the gameplay rules. Refuses to overwrite a source.
blender -b --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- new facility refinery

# A local planned template before gameplay data exists; CI requires the identifier in data.
blender -b --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- new facility future_module --tiers 2

# Edit and save the source. Check writes nothing; export requires a valid saved file.
blender -b Design/Art/facilities/crew_quarters/crew_quarters.blend --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- check
blender -b Design/Art/facilities/crew_quarters/crew_quarters.blend --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- export

python3 Design/Art/tools/catalogue.py
python3 Design/Art/tools/catalogue.py --check
python3 Design/Art/tools/check_exports.py
python3 -m unittest discover -s Design/Art/tools/tests -p test_check_exports.py
blender -b --python-exit-code 1 --python Design/Art/tools/tests/test_frontier_art.py
```

The one-time `split` command accepts `--source <pre-M8 scene.blend>` and refuses to replace
existing migrated sources. The current frozen scene intentionally has no facility roots left.

## Authoring and exports

Each tier and tell stays at its own origin; the game supplies its placement. They overlap in
the source: hide other roots to inspect or edit one, then unhide them before checking/exporting.
The tool temporarily lays them out side by side for its 512-pixel review preview and never saves
that layout into the source or gameplay GLB.

Blender requires globally unique object names. Operating meshes use
`Operating_Tier_<n>__<root name>`, for example `Operating_Tier_1__Facility_crew_quarters_T2`.
Tier 2 has separate tier 1 and tier 2 indicators. Presentation cameras and lights never enter
the GLB. Station anchors are also recorded in the manifest. Bounds are root-local Blender
Z-up metres, not glTF Y-up. Hull footprint radii conservatively enclose their bounds.

Background export reopens the saved source before checking it, so source hashes and exported
geometry agree. Interactive export rejects unsaved changes. Exports are staged before replacing
previous outputs; repeat exports with matching inputs and output hashes make no changes.
There are no timestamps in manifests. A tool, contract or library change requires re-exporting
affected sources and regenerating the catalogue.

Keep the whole `Design/Art/` folder when moving sources: linked material paths are relative.
`model_crew_quarters.py` reproduces the initial example from an empty two-tier template and
refuses to replace existing geometry. It is an authoring aid, not a required export step;
manual modelling and editing are supported.
