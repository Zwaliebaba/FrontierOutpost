# Frontier Outpost — game scene foundation

Art direction: [Frontier Outpost Visual Style Guide](../Visual-Style-Guide.md). Game design: [Frontier Outpost](../FrontierOutpost.md).

A HUD-free outpost scene: an octagonal orbital depot with two circular side pods, a numbered forward landing deck, a service spine, antenna mast and red pennant; the original Aurora ship preserved as a cruiser in the background; a new starter frigate landed on berth 07; faceted asteroids; a distant blue planet and procedural starfield.

![Rendered overview](FrontierOutpostPreview.png)

## Files

- `FrontierOutpost.blend`: editable scene (saved by Blender 4.3; opens in 5.x), overview camera and lighting.
- `FrontierOutpostPreview.png`: rendered overview.
- `starter_frigate_detail.png`: close-up of the starter frigate on its landing gear.
- `station.glb`, `cruiser.glb`, `frigate.glb`, `asteroids.glb`: separate gameplay asset exports. `cruiser.glb` preserves the original Aurora hull at its original scale; `frigate.glb` is the new starter ship.
- `create_scene.py`: deterministic source generator, seed 2266.
- `scene_manifest.json`: mesh statistics, scale and axis conventions.

## Rebuild

```sh
blender -b -t 8 --python create_scene.py
```

The script runs on Blender 4.3+ and 5.x (verified on 5.2.3 LTS, which reproduces the committed manifest exactly). The committed `.blend` and GLBs were generated with Blender 4.3; regenerating them on a newer Blender changes their bytes, not their geometry, so only commit regenerated binaries together with a reason. The script writes files alongside itself by default. Use `SCENE_OUTPUT` to choose a different directory, `RENDER_SAMPLES` to adjust quality, `RENDER_SCALE` for resolution percentage, or `SKIP_RENDER=1` to generate assets without rendering. No external textures, downloads or add-ons are required. Rendering uses Cycles, with denoising enabled only when the Blender build supports OpenImageDenoise.

## Game import

Use metres: Blender Z-up, ships forward +Y. The glTF exporter converts to glTF Y-up. `Station_ROOT`, `Cruiser_ROOT`, and `Frigate_ROOT` are assembly pivots; child meshes remain separate for editing, destruction or interaction. The GLBs retain the scene's placement so they line up when imported together. Move each assembly root to the origin to use it as a standalone prefab. Asteroids are individually named mesh objects.

## Ship classes and docking

Aurora is now a cruiser: its original hull, armor, equipment, materials and scale are retained, with a new background position. Its blue engine spill lights follow its assembly root. The starter frigate is a separate, simpler design with the same split armor, orange band and twin drive layout. It has a compact bridge, one small defense mount and deployed tripod landing gear. Its engines idle while landed.

[See the starter frigate close-up](starter_frigate_detail.png). The Blender file also includes a detail camera for inspecting the new ship.

The frigate is aligned with the pad's longitudinal axis. The generator measures its complete evaluated geometry, including bevels and landing feet, in station coordinates and checks that it stays at least 0.15 scene metres inside the painted safety perimeter. The feet are placed at station-local Z=0.86 to clear the deck's raised painted markings. Measurements are recorded in `scene_manifest.json` under `docking_validation`. Exhaust is off and excluded from the landed frigate's footprint. This validates geometric fit; docking gameplay and animations still need implementation in the game engine.

The station's rear pad connector is flush with the bare deck at Z=0.83, leaving the landing area unobstructed. The generator also checks the complete frigate bounds against every structural station mesh; decorative paint overlays are excluded from this overlap check.

GLBs use standard metal/roughness materials and emissive windows. The camera, planet, starfield, nebula, lights and translucent engine exhaust are presentation elements in the Blender file, excluded from the gameplay exports. Recreate bloom and exhaust in the target engine. The GLB exports contain no gameplay scripts, collision shapes or baked lightmaps.

This is an editable visual foundation, not a fully optimized production asset pack. Before shipping, combine repeated details or instance them, add collision meshes and LODs, atlas materials if needed, and profile against the target engine and hardware. Windows and small greebles intentionally remain separate for easy revision. There is no HUD, ship control, docking logic or other gameplay implementation yet.

## Scene organization

`Station`, `Cruiser`, `Frigate`, `Asteroids`, `Backdrop`, `Lighting`, and `Cameras` collections isolate gameplay geometry from presentation. Materials are named by role. Geometry uses restrained bevels and flat-shaded rock facets to echo the reference's stylized industrial art direction.
