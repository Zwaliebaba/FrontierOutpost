# Frontier Outpost — game scene foundation

Art direction: [Frontier Outpost Visual Style Guide](../Visual-Style-Guide.md). Game design: [Frontier Outpost](../FrontierOutpost.md).

A HUD-free outpost scene: an octagonal orbital depot with two circular side pods, a numbered forward landing deck, a service spine, antenna mast and red pennant; the original Aurora ship preserved as a cruiser in the background; a new starter frigate landed on berth 07; faceted asteroids; a distant blue planet and procedural starfield.

![Rendered overview](FrontierOutpostPreview.png)

## Files

- `FrontierOutpost.blend`: editable scene (saved by Blender 5.2.3 since the construction sockets were added; it may not open in 4.x, but the script still runs there), overview camera and lighting.
- `FrontierOutpostPreview.png`: rendered overview.
- `facilities.glb`: the three facility modules at two tiers each (plan item 5.1), exported apart from the station.
- `facilities_preview.png`: the six modules in a row, rendered from the facility catalogue camera.
- `starter_frigate_detail.png`: close-up of the starter frigate on its landing gear.
- `station.glb`, `cruiser.glb`, `frigate.glb`, `asteroids.glb`: separate gameplay asset exports. `station.glb` was regenerated with Blender 5.2.3 when the sockets were added; the other three are still the 4.3 exports, with the same geometry the script produces now. `cruiser.glb` preserves the original Aurora hull at its original scale; `frigate.glb` is the new starter ship.
- `create_scene.py`: deterministic source generator, seed 2266.
- `scene_manifest.json`: mesh statistics, scale and axis conventions.

## Rebuild

```sh
blender -b -t 8 --python create_scene.py
```

The script runs on Blender 4.3+ and 5.x (verified on 5.2.3 LTS, which reproduces the committed manifest exactly). The committed `.blend` and GLBs were generated with Blender 4.3; regenerating them on a newer Blender changes their bytes, not their geometry, so only commit regenerated binaries together with a reason. The script writes files alongside itself by default. Use `SCENE_OUTPUT` to choose a different directory, `RENDER_SAMPLES` to adjust quality, `RENDER_SCALE` for resolution percentage, or `SKIP_RENDER=1` to generate assets without rendering. No external textures, downloads or add-ons are required. Rendering uses Cycles, with denoising enabled only when the Blender build supports OpenImageDenoise.

## Game import

Use metres: Blender Z-up, ships forward +Y. The glTF exporter converts to glTF Y-up. `Station_ROOT`, `Cruiser_ROOT`, and `Frigate_ROOT` are assembly pivots; child meshes remain separate for editing, destruction or interaction. The GLBs retain the scene's placement so they line up when imported together. Move each assembly root to the origin to use it as a standalone prefab. Asteroids are individually named mesh objects.

## Construction sockets and facility modules

Design section 10's starter outpost has two construction sockets. `Socket A` sits beside the cargo bay (the port pod, on the -X side) and `Socket B` beside berth 07. Each is a clamp platform: an octagonal deck 1.3 scene metres in radius, a yellow mounting guide, four clamps on the rim along the axes, struts and a bridge with a power conduit to the station. An empty named `Socket_A` or `Socket_B`, a child of `Station_ROOT`, marks the centre of each deck. A module's base goes there. The manifest records both anchors in station space.

`facilities.glb` holds six roots, `Facility_<facility>_T<tier>`, named by the game's facility identifiers (`sensor_array`, `refinery` and `fabrication_bay`) at tiers 1 and 2. Each root carries `facility` and `tier` custom properties. Each module is built around its root with its base at the root's origin. Its footprint stays within 1.25 scene metres of the centre on both horizontal axes, inside the rim clamps. Every tier 2 module stands more than 0.4 scene metres taller than its tier 1, so a tier change reads at the zoom where the whole outpost fits (design section 2). The generator asserts all three rules. In the Blender file the modules stand in a catalogue row out of the overview camera's frame. Move a root to a socket anchor (or its position to zero, under the anchor) to install it.

- **Sensor array.** Tier 1 is a plinth with a single dish on a mast. Tier 2 adds a lattice tower with two dishes and a phased array on top.
- **Refinery.** Tier 1 is two tanks, a manifold and a stack on a skid. Tier 2 has four tanks, a crusher hopper, twin stacks and a catwalk.
- **Fabrication bay.** Tier 1 is a hangar with one door and a crane arm. Tier 2 is a larger hangar with two doors, an overhead gantry and a robot arm.

## Ship classes and docking

Aurora is now a cruiser: its original hull, armor, equipment, materials and scale are retained, with a new background position. Its blue engine spill lights follow its assembly root. The starter frigate is a separate, simpler design with the same split armor, orange band and twin drive layout. It has a compact bridge, one small defense mount and deployed tripod landing gear. Its engines idle while landed.

[See the starter frigate close-up](starter_frigate_detail.png). The Blender file also includes a detail camera for inspecting the new ship.

The frigate is aligned with the pad's longitudinal axis. The generator measures its complete evaluated geometry, including bevels and landing feet, in station coordinates and checks that it stays at least 0.15 scene metres inside the painted safety perimeter. The feet are placed at station-local Z=0.86 to clear the deck's raised painted markings. Measurements are recorded in `scene_manifest.json` under `docking_validation`. Exhaust is off and excluded from the landed frigate's footprint. This validates geometric fit; docking gameplay and animations still need implementation in the game engine.

The station's rear pad connector is flush with the bare deck at Z=0.83, leaving the landing area unobstructed. The generator also checks the complete frigate bounds against every structural station mesh; decorative paint overlays are excluded from this overlap check.

GLBs use standard metal/roughness materials and emissive windows. The camera, planet, starfield, nebula, lights and translucent engine exhaust are presentation elements in the Blender file, excluded from the gameplay exports. Recreate bloom and exhaust in the target engine. The GLB exports contain no gameplay scripts, collision shapes or baked lightmaps.

This is an editable visual foundation, not a fully optimized production asset pack. Before shipping, combine repeated details or instance them, add collision meshes and LODs, atlas materials if needed, and profile against the target engine and hardware. Windows and small greebles intentionally remain separate for easy revision. There is no HUD, ship control, docking logic or other gameplay implementation yet.

## Scene organization

`Station`, `Cruiser`, `Frigate`, `Asteroids`, `Backdrop`, `Lighting`, and `Cameras` collections isolate gameplay geometry from presentation. Materials are named by role. Geometry uses restrained bevels and flat-shaded rock facets to echo the reference's stylized industrial art direction.
