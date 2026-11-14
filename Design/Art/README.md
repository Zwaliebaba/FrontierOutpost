# Frontier Outpost art

From plan M8 every new asset is modelled by hand in Blender by the owner, one file per asset,
and goes through one pipeline that checks it, exports it and records it, so the game finds it
without code changes. The pipeline is built to hold tens of facility modules: adding one is a
model, an export and a line of game data.

The generated scene in [`Design/FrontierOutpost/`](../FrontierOutpost/README.md) is frozen. Its
assets stay until each is replaced here (see the last section), and `create_scene.py` changes only
to remove what has moved. The agents write the pipeline's tools and the game code; they never edit
a `.blend`, an exported GLB or a manifest, and their environment has no Blender.

This document is also the art brief for the Stage 1 expansion (design §11, plan M8 to M12). Where
it sets a visual convention, the [style guide](../Visual-Style-Guide.md) records it too.

## Layout

```
Design/Art/
  README.md                 this document
  contract.json             every number the rules use (below)
  catalogue.json            every asset the game can load; assembled, never edited
  tools/frontier_art.py     the Blender tool: new, check, export
  library/materials.blend   the shared materials, linked into every asset file
  facilities/<id>/          one folder per facility
    <id>.blend              the owner's source: every tier and every tell
    <id>.glb                the export: every tier and every tell
    <id>.json               the export's manifest
    <id>.png                a small preview, for review
  hulls/<id>/               the same four files per hull
  station/exterior/         the same four files for the station's exterior
  station/interior/         the same four files for the station's interior
```

`<id>` is the game's identifier for the asset (`refinery`, `crew_quarters`, `hauler`), the same
in the folder, the files, the roots and the game's data.

## Asset kinds

| Kind | Roots in the file, one per exported object | Custom properties on each root |
|---|---|---|
| `facility` | `Facility_<id>_T<n>` for each tier, `Tell_<id>_T<n>` for each tier's tell | `frontier_kind`, `frontier_id`, `tier` |
| `hull` | `Hull_<id>` | `frontier_kind`, `frontier_id` |
| `station_exterior` | `StationExterior_ROOT` | `frontier_kind` |
| `station_interior` | `StationInterior_ROOT` | `frontier_kind` |

The tool reads `frontier_kind` to know which rules apply. One file, one asset: a facility's file
holds all its tiers and tells and nothing else.

## The contract

`contract.json` holds every number the rules below use: the socket envelope (a module's
half-width, 1.25 m today, and the tallest a module may stand), the tier step (each tier more than
0.4 m taller than the one below), the tell cube (1.5 m), the hangar mouth's minimum opening
(6 × 3 m) and approach lane (30 m), the clearance (0.15 m), the triangle budgets per kind, the
material roles, the names of runtime nodes, and the interior's scale once M9 sets it. The tool,
the CI check and the game all read this one file, so changing a number re-checks every asset.
The values are placeholders, like the design's.

**Any module fits any socket.** The envelope is the same for every socket, so adding a module
never touches the interior, and adding a socket never touches a module. This is what lets the
module count grow.

## The tool

`tools/frontier_art.py` runs inside Blender 4.3+ or 5.x:

```sh
blender -b --python Design/Art/tools/frontier_art.py -- new facility crew_quarters
blender -b Design/Art/facilities/refinery/refinery.blend --python Design/Art/tools/frontier_art.py -- check
blender -b Design/Art/facilities/refinery/refinery.blend --python Design/Art/tools/frontier_art.py -- export
```

- **`new <kind> <id>`** starts a file from the kind's template: the collections, the roots with
  their properties (one per tier the game's data defines, for a facility), the anchors the kind
  needs, the material library linked, and a camera at the resting tactical view (33°,
  orthographic), so the silhouette is judged as the game shows it.
- **`check`** applies every rule for the file's kind, from `contract.json`, prints each failure
  and writes nothing.
- **`export`** runs `check` and, only if it passes, writes the GLB, the manifest (each root's
  bounds, triangles, materials and named nodes, the GLB's SHA-256, the tool's and Blender's
  versions) and a 512-pixel preview.
- **`split`**, used once (8.3), moves the six generated facility modules out of the frozen scene
  into their own facility files, as starting points the owner then refines by hand.

## The catalogue and CI

- `catalogue.json` is assembled from every manifest by a repository script that needs no
  Blender, and CI fails when it is out of date, as it does for the generated protocol types.
- CI checks, without Blender, that every manifest's GLB exists and matches its SHA-256, so what is
  committed is what the tool checked; that every facility in the game's data has a facility asset
  with every tier the data defines and a tell for each, unless the facility is marked
  `art: pending`; that every asset's identifier is in the data; and that budgets and names follow
  the contract.
- The client finds art only through the catalogue, with no import per asset, and loads a
  facility's GLB the first time it is shown, so tens of modules cost nothing until used.
- A facility marked `art: pending` in the game's data cannot be built in play. Its rules can
  merge before its model, and the model is the last step.

## Adding a facility, start to finish

1. The agents add the facility to the game's data, marked `art: pending`, with its rules and
   tests.
2. The owner runs `new facility <id>` and models each tier and each tell.
3. The owner runs `export`, regenerates the catalogue, and commits the folder and the catalogue.
4. The `art: pending` mark comes off; CI proves the art is complete.

No game code changes for a new module.

## Rules for every asset

- Metres, Z-up, forward +Y. Each root at its own origin; the game places it.
- **Materials are linked** from `library/materials.blend`, one per role (style guide §5): hull
  tones, armour, steel, the orange band, warm and cyan emissives. The tool rejects any other
  material, so tens of models keep one palette. Standard metallic/roughness.
- **Runtime nodes** stay separate meshes with the names the contract lists
  (`Operating_Tier_<n>`, `Closure_*`, `Reactor_Upgrade*`), so the game can switch them.
- No scripts, collision shapes, lights or cameras in an export; effects such as exhaust stay out.
- Within the kind's triangle budget (about 6,000 for a module tier or a hull, 500 for a tell,
  30,000 for the station's exterior or interior, as reference figures; style guide §9).
- **Commit only what changed:** an export only when its manifest changed; previews are small.

## Facility modules and their tells

- **Each tier** is a root `Facility_<id>_T<n>` with its base at the origin, within the socket
  envelope, nothing below the base, under the tallest a module may stand, and more than the tier
  step taller than the tier below. It carries an `Operating_Tier_<n>` indicator for each tier up
  to its own.
- **Each tier's tell** is a root `Tell_<id>_T<n>`, a small marker in the facility's signature
  shape within the tell cube, bigger at each tier. The game sets it on the exterior's hardpoint
  for the socket the facility is installed in, so a facility is recognised outside and in.
- **Each facility keeps a signature shape** no other uses, told apart at the zoom where the whole
  station fits: dishes are the sensor array's, a closed hall with doors the fabrication bay's, an
  open cradle the shipyard's. No tool can judge a shape, so the owner reviews each new facility's
  preview beside the others before its `art: pending` mark comes off.

| Facility | Tier 1 | Tier 2 | Tier 3 |
|---|---|---|---|
| Sensor array | A plinth with a single dish on a mast | A lattice tower with two dishes and a phased array | A large radome on the lattice (10.1) |
| Refinery | Two tanks, a manifold and a stack on a skid | Four tanks, a crusher hopper, twin stacks and a catwalk | A fuel-cell plant: pressure spheres and a cracking column (10.1) |
| Fabrication bay | A hall with one door and a crane arm | A larger hall with two doors, a gantry and a robot arm | A plate press and stacked armour plates, with a second gantry (10.1) |
| Crew quarters (8.4) | A pressurised habitat: two horizontal cylinders with warm windows and an airlock | A second deck and an observation dome; a davit for the wingman's shuttle | None |
| Shipyard (11.1) | An open lattice cradle holding a hull frame, with a gantry over it | Two cradles, a longer gantry and fuel-cell racks | None |
| Navigation array (12.1) | A transmitter spire ringed by beacon lamps, with a star-tracker tube | Twin rings round the spire and a longer telescope | None |

Tiers 1 and 2 of the first three facilities exist, generated; 8.3 moves them here.

## The station's exterior (9.2)

The station is seen two ways (design §11). The exterior stands at the dock in every run and at
home, with no sockets, modules, berths or parked ships.

- **Root and scale:** `StationExterior_ROOT` at the dock point, the hangar mouth facing +Y; two to
  three times today's station, the factor chosen in the greybox first (9.1). Ships keep their size.
- **One hangar mouth:** an empty `Hangar_Mouth` at the centre of its opening on the landing plane,
  facing out. Its clear opening meets the contract's minimum and is wider and taller than the
  largest hull in the catalogue with the clearance on every side, so crew ships can leave one
  after another.
- **A clear approach lane** straight out of the mouth, as wide as the opening and as long as the
  contract says, free of every exterior mesh and of every tell at its largest.
- **Tell hardpoints belong to the sockets, not the facilities:** an empty per interior socket
  (`Tell_A`, `Tell_B`, `Tell_C`) and `Tell_reactor`, on the hull's surface, visible from the
  resting camera, and far enough from the lane that the largest tell clears it. The tell of
  whichever facility is installed in a socket stands on that socket's hardpoint, so the exterior
  never changes for a new facility, however many there are; only a new socket needs a hardpoint.
- **Readable from the tactical view.** A strong central mass and restrained detail (style guide
  §1, §2). Tall spires and dense lit detail that work in a perspective hero shot become clutter at
  33° orthographic and hide ships; the greybox (9.1) decides how tall the station may stand.

## The station's interior (9.3)

A cutaway of the same station, open to the resting view, seen through the one tactical camera
(AGENTS.md §4.5), where the modules and the owned ships are shown and building happens.

- **Its parts:** sockets A, B and C, each an empty `Socket_<letter>` at its deck, with C closed by
  `Closure_C*` until the reactor is bought; the hangar deck with an empty `Parking_<hull>` per
  hull in the catalogue; the processor (a hopper, a furnace drum and an output chute; design §10
  names it, but no scene has modelled it); the cargo bay; and the reactor in meshes named
  `Reactor_Upgrade*`, shown once bought (10.1). Socket A sits beside the cargo bay and socket B
  beside the hangar deck, so their adjacency rules still describe the place.
- **Nothing hides anything.** With the tallest module the contract allows in every socket and
  every hull in its parking place, nothing covers another socket or parking place from the
  resting camera. The tool checks it along the resting view direction.
- **A new hull needs a parking place** added here; the CI check fails until it has one.

## Hulls (11.1)

New hulls join the starter frigate's family (style guide §3): split dorsal armour, a central
service spine, orange bands, dark inset panels and twin drive pods, with proportions and
equipment changed for the role. None may read as a hostile: allegiance is told by shape as well
as colour, and hostiles are forked silhouettes.

| Hull | Silhouette |
|---|---|
| Hauler | Wide and boxy: two cargo pods slung either side of the spine, a short bridge, no visible guns |
| Gunship | About the frigate's length, with forward weapon mounts, a turret on the spine and a shield emitter ring |

- **Root:** `Hull_<id>`, nose along +Y.
- **Through the mouth and into its place:** each hull passes the exterior's mouth flying straight
  in, and fits its interior parking place, with the clearance on every side, weapons and
  attachments included. The manifests carry the bounds; the exterior's and interior's checks use
  the largest hull in the catalogue.
- **No landing gear** is needed: ships are never drawn landed once the berths are gone (9.6).
- **The collision radius comes from the model:** the manifest records the hull's footprint radius,
  and the game's hull data (11.2) takes its collision radius from it.

## What is still generated, and when it moves

| Generated asset | Replaced here in |
|---|---|
| The six facility modules | 8.3, split into their facility files, then refined by hand at will |
| The four-berth station | 9.2 and 9.3, by the exterior and the interior |
| The starter frigate | 11.1, as `hulls/frigate/` |
| The asteroids and the cruiser | Whenever they next change |

Hostiles (including 12.3's sentinel and stalker) are drawn by the client's readability rules and
have no models yet.
