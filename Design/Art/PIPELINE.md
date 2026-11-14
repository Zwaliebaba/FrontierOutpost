# Running the first M8 pipeline

This implementation uses the shared Blender instance to author files, as requested by the
owner in this session. This supersedes the older owner-only modelling/environment assumption
in the art brief for this work. Sources remain editable; binaries and manifests are generated
by Blender and the export tool, never edited as binary data or by hand.

## What works now

- Blender 4.3.2 has generated the shared linked material library and the first two-tier
  crew-quarters source, GLB, manifest and 512 × 512 preview.
- Facility and hull templates, validation and exports work. Preview cameras use exactly the
  game's view direction `(0.43, -0.72, 0.545)` in Blender Z-up space, approximately 33° elevation.
- Facility tier counts come from the current literal `STAGE1` rules through `game_data.py`.
  This reader fails if it cannot recognise that format. Planned facilities require `--tiers`;
  they are marked `planned` in their manifest and are not registered or buildable in the game.
- CI verifies committed export/source/library/tool/contract hashes, tier roots and budgets.
  It does not run Blender or prove the manifest's geometric claims independently.

This is the first M8.1 delivery, not completion of M8. Station templates contain anchors,
but station checks and exports deliberately fail with `station_pending`: M9 must establish
the station scale, hangar and hull clearance, parking and socket occlusion tests first.
The catalogue, client lazy loading and complete gameplay-art coverage are M8.2; migration
of the six existing facility modules is M8.3. Existing runtime assets are untouched.

The reference station image guides the future broad ring, heavy hub, segmented armour,
warm windows and illuminated hangar. This change does not choose its M9 dimensions.

## Commands

Run from the repository root. `blender` must be on PATH. On Windows, use the full path to
`blender.exe` if needed, and `py` for the ordinary Python checks.

```sh
# One-time library creation; refuses to replace an existing library.
blender -b --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- library

# Existing gameplay facility: derives its tier count from the game rules.
blender -b --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- new facility refinery

# Planned facility, before its gameplay data is implemented.
blender -b --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- new facility crew_quarters --tiers 2

# Open and edit the resulting source, then save it. Check writes no files.
blender -b Design/Art/facilities/crew_quarters/crew_quarters.blend --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- check
blender -b Design/Art/facilities/crew_quarters/crew_quarters.blend --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- export

# Ordinary Python, same check CI runs.
python3 Design/Art/tools/check_exports.py
python3 -m unittest discover -s Design/Art/tools/tests -p test_check_exports.py

# Actual Blender fixtures, including GLB export, idempotence and broken-rule cases.
blender -b --python-exit-code 1 --python Design/Art/tools/tests/test_frontier_art.py
```

`new` refuses to overwrite a source. Templates contain roots and linked materials, not final
geometry: an empty template should fail `check`. Each facility tier and tell stays at its own
origin; export lays them out temporarily for the review preview, without saving that layout.
To view individual tiers in Blender, hide the other roots while editing; unhide before export.

Blender requires globally unique object names. Operating meshes therefore use
`Operating_Tier_<n>__<root name>` (for example
`Operating_Tier_1__Facility_crew_quarters_T2`), retaining the runtime role without `.001`
suffixes. Tier 2 has separate tier 1 and tier 2 indicators. Presentation cameras and lights
live outside the `Export` collection and never enter the GLB.

Background export reopens the saved `.blend` first, so the manifest's source hash always
describes the geometry actually exported. In an interactive Blender session, unsaved changes
are rejected. Export stages its outputs before replacing them and skips a repeat export when
the inputs, geometry records and output hashes match. Input changes trigger a fresh export;
there are no timestamps in manifests. Bounds are root-local Blender Z-up metres, not glTF
Y-up. Hull footprint radii conservatively enclose the bounds' horizontal corners.

Keep linked library paths relative to the source. Move or clone the whole repository rather
than a `.blend` alone. The material library is generated from `contract.json`; if its palette
changes, explicitly version/regenerate the library and all affected exports together.

`model_crew_quarters.py` reproduces the initial example from an empty crew-quarters template.
It refuses to replace existing geometry. It is an authoring aid, not a step to run on every
export; future manual editing of the source is supported. The example still needs visual
review and gameplay integration before being marked ready for play.
