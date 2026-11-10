# Frontier Outpost — Visual Style Guide

Working art direction and environment design standards for the game's Blender scenes. This document describes the visual design language: how ships, stations, asteroids, materials, lighting and effects should work together.

## 1. Art direction

Build readable, stylized industrial space environments. Ships and stations should look assembled from practical machinery: armored pressure hulls, service spines, external equipment, clear docking surfaces and visible propulsion. Use restrained detail and strong silhouettes that remain readable from a tactical camera.

The established look combines blue-grey naval armor, graphite structure, pale titanium edges, orange identification bands, warm inhabited windows and blue propulsion. Space is dark blue rather than featureless black. Asteroids are irregular, angular masses with visible triangular facets.

Each scene should communicate one activity immediately: arriving at a station, landing, or extracting ore. Keep the subject, its destination and any working equipment visually connected.

## 2. Shape language and detail hierarchy

| Layer | Design pattern | Existing examples |
| --- | --- | --- |
| Primary forms | Simple boxes, tapered hulls, octagonal drums and large faceted rocks | Frigate keel, cruiser body, station hub, main mining asteroid |
| Secondary forms | Segmented armor, bridge blocks, side pods, ribs and equipment mounts | Split dorsal armor, circular station pods, mining arms |
| Tertiary detail | A few fasteners, vents, lights, short aerials and thin markings | Radiator fins, navigation lights, pad stripes |

Establish the primary silhouette before adding detail. Use dark recesses between armor plates to separate forms. Keep bevels small and mostly single-segment so edges catch light while preserving the angular style. Machinery should have a plausible attachment point. Repeated details should follow a structural rhythm rather than cover every surface.

Keep asteroid faces flat-shaded. Vary their proportions and vertex positions; distribute material values across facets. Reserve smoother geometry for circular rails, lights and selected manufactured parts.

## 3. Ship family and progression

Ships in the same family share split dorsal armor, a central service spine, orange bands, dark inset panels and twin drive pods. Change proportions and equipment as the class grows.

| Class | Visual identity | Role in the current scenes |
| --- | --- | --- |
| Starter frigate | Compact hull, short bridge, few armor sections, one small defense mount, limited equipment | Player's early ship; fits berth 07; accepts removable mining tools |
| Aurora cruiser | Longer segmented hull, larger shoulders, extended spine and more exposed machinery | Preserved original ship; approximately four times the starter's length |

The cruiser remains a separate asset at its original scale. Future small ships should be designed around their role and landing envelope instead of uniformly shrinking the cruiser. Preserve the starter hull's recognizable silhouette when adding equipment.

Flight configurations retract landing gear and close its bays. Landed configurations deploy gear, idle the engines and align with the berth. Utility rigs should attach to explicit hardpoints and remain removable from the base ship.

### Alien faction contrast: Veyr

Veyr is the working name for the first alien visual concept. Its frigate is a peer-sized opponent with a different construction language: a narrow central keel framed by two swept crescent blades, layered ivory carapace over dark jade structure, bronze seams and a faceted command crystal. Keep it angular and practical enough to belong in the same low-poly world, while changing the silhouette and panel rhythm as well as the colors.

| Feature | Human starter family | Veyr concept |
| --- | --- | --- |
| Silhouette | Narrow longitudinal hull, block armor, twin rear pods | Broad crescent, swept split prow, central crystal |
| Armor and structure | Blue-grey, titanium and graphite | Ivory, jade and dark internal frames |
| Identification detail | Burnt-orange bands and visible fasteners | Bronze seams and inlaid jade conduits |
| Propulsion | Blue tapered plumes | Violet tapered plumes |
| Weapons | Discrete blue-white pulse bolts | Chunkier magenta plasma bolts |
| Shield response | Cyan local patch | Jade local patch |

Use this as a reusable faction distinction rather than recoloring a human hull. Preserve comparable ship-class scale so a frigate duel remains readable. The name and faction lore remain open for later game design.

## 4. Scale, docking and clearance

Maintain consistent relative scale across scenes. The current Blender files use metre units, Z-up and ship-local +Y forward; glTF exports use Y-up. These are the current working scene dimensions. Choose any later physical-scale conversion once for the whole game, including ships, stations, collision, lights and movement speeds.

At the current working scale, the starter frigate's landed envelope is approximately 1.29 wide × 2.67 long × 1.14 high. Berth 07's painted safety perimeter is 4.08 wide × 3.06 long. Preserve at least 0.15 units of horizontal safety clearance, including gear and attachments; scale that margin with the project.

Check the complete evaluated geometry, including bevels and equipment. A hull fitting inside a painted rectangle is only one check: connectors, rails, masts and other station structure must also clear the ship. Keep approach surfaces flush and unobstructed. Flight scenes also need physical separation between the hull, tools and nearby rocks.

Effects do not define the hull envelope. Keep exhaust, beam halos and particles separate from solid gameplay geometry.

## 5. Materials and color roles

These are the current Blender base-color RGB values, not display hex colors. Treat them as reusable material baselines; illumination and AgX determine the rendered appearance.

| Material role | RGB baseline | Use |
| --- | --- | --- |
| Midnight hull steel | 0.095, 0.135, 0.190 | Main pressure hull and intermediate structure |
| Blue-grey armor | 0.290, 0.350, 0.430 | Large armor plates |
| Pale titanium | 0.530, 0.570, 0.610 | Selected panels, edge contrast and machinery |
| Graphite | 0.025, 0.038, 0.055 | Recesses, frames, radiator beds and mounts |
| Burnt orange | 0.620, 0.190, 0.055 | Identification bands and utility attachments |
| Safety ochre | 0.950, 0.570, 0.120 | Berth borders and approach markings |
| Warm amber | 1.000, 0.400, 0.055 | Windows and operating station lights |
| Cyan | 0.040, 0.650, 1.000 | Navigation and utility indicators |
| Ion blue | 0.015, 0.190, 1.000 | Active propulsion |
| Copper ore | 0.480, 0.200, 0.055 | Exposed metallic deposits |

Manufactured hull materials use moderate-to-high metallic values and visible roughness. Keep graphite parts distinguishable from the background. Ore is reflective copper, iron and pale pyrite embedded in darker basalt. Unworked ore should catch light rather than glow. Use emission for active equipment, hot contact points, engines and windows.

## 6. Lighting and camera

Use cool starlight for the broad illumination, a restrained warm rim for industrial surfaces, and soft blue fill to preserve hull readability. Small amber and blue sources should illuminate their surroundings locally. Avoid lighting every surface equally: shadows help separate plates and machinery.

Use AgX and restrained fog glow in Blender. Bloom should reinforce hot points and beams without obscuring their geometry. Bright lights need a readable colored edge around the hottest core.

The shared tactical view is orthographic, at roughly 33 degrees of elevation. Keep +Z vertical and preserve a consistent sense of ship heading. The outpost overview uses a wide frame; the mining scene uses a slightly taller frame to give the asteroid belt room. Detail cameras can move closer while retaining the same viewing direction.

Compose clear foreground, action and background layers. Large cropped rocks can frame a corner; smaller distant rocks establish depth. Leave space around the ship silhouette and the activity's contact point. A mining beam should remain visible between its emitter and the rock. Background stars and nebulae should support the subject with quiet contrast.

## 7. Scene patterns

### Outpost and docking

Use a strong central station mass, secondary side pods and a clearly readable apron. Warm windows imply occupation. Navigation lamps and painted borders indicate where a ship can land. Place large vessels outside starter berths, with enough separation to make their class difference apparent. The starter ship can demonstrate actual berth fit.

### Mining and resource gathering

Place one ore-bearing asteroid within working range of the starter frigate, with ordinary rocks around it for context. Make the exposed ore legible through material contrast and faceted outcrops. Keep the frigate's gear retracted and its drive glow short while it holds position.

Twin industrial drill mounts produce warm amber cutting beams. Each beam starts at an emitter lens and stops at a ray-cast surface hit on the actual asteroid mesh. Place dark excavation scars, small hot rims and short outward sparks at those hits. Keep extracted chips close enough to read as fragments from that rock. A subtle cyan retrieval tether may indicate collection.

Use these effect roles consistently:

| Effect | Color and shape | Meaning |
| --- | --- | --- |
| Propulsion | Blue core with a tapered plume | Ship thrust |
| Mining cut | Narrow amber-white line with a restrained orange halo | Active extraction tool |
| Impact heat | Small orange rim and short warm spark streaks | Local material heating |
| Ore retrieval | Thin cyan tether | Collecting a loose fragment |
| Human pulse weapon | Separated blue-white short streaks | Human defensive fire |
| Veyr plasma weapon | Magenta short bolts | Alien offensive fire |
| Shield hit | Local translucent cyan or jade patch with energy ripples | Shield absorbing incoming fire |

Keep the impact readable and localized. Place solid mineral outcrops on the rock surface. Separate ordinary ore fragments from incandescent sparks. Do not add a station or HUD to an open-space mining vignette unless that becomes the scene's intended activity.

### Frigate combat

Face the ships toward one another with a clear firing corridor and some vertical separation in the composition. Show both hull silhouettes and their identifying materials. Small banking angles and tapered drive plumes can imply maneuvering while retaining the shared tactical camera.

Distinguish weapons from mining tools through shape and timing: combat uses separated short pulse or plasma streaks, while industrial extraction uses continuous warm cutting beams. Spawn fire from real weapon sockets. Local shield patches, bright contact points and short energy arcs communicate a hit without hiding the ship inside an opaque bubble.

Incoming projectiles terminate at a defined shield surface. Keep shield effects separate from the hull and use consistent faction colors for their response. Energy arcs should read as dissipating shield energy; reserve physical armor debris and hull damage for a scene that actually depicts a breached shield. Background rocks should frame the fight and leave the weapons and impact regions legible.

The combat scene reuses the original starter hull, retracts its gear and aims its existing turret. Alien geometry and faction materials are separate reusable assets. Neither scene setup nor visual effects constitute a gameplay combat system; AI, movement, targeting, damage and shield rules belong in the engine implementation.

## 8. Asset and scene structure

### Orbital Sector 7 technical treatment

This reference-inspired scene introduces an optional technical visualization treatment. Use dark solid hulls with fine blue or red edge accents, subtle teal asteroid facet edges, warm amber windows and restrained engine glow. Keep these as real 3D surface details that share the camera and depth of the objects. Preserve the broad lighting and tactical camera conventions while giving the new scene its own reference-matched station and patrol hulls.

Use a large open ring station, vertical communications tower and underslung spine as the primary landmark. Place five blue vessels in the central formation, four smaller red vessels in the distance, and frame them with asteroid installations and one large diagonal foreground rock. A blue crescent planet establishes the background. Keep distant factions readable through both color and composition.

The edge accents are presentation geometry batched per assembly, and can later be replaced by an engine shader. They do not substitute for solid surfaces. Omit all screen-space UI: menus, health bars, minimaps, selection rings, destination markers and dashed travel paths. Those belong to the later HUD implementation. The original shaded industrial scenes and their saved ship assets remain intact; this is an additional visual treatment, not a replacement of their design rules.

### Shared assembly conventions

Use named collections for ships, equipment, rocks, effects, backdrop, lighting and cameras. Keep each reusable assembly under an explicit `*_ROOT` pivot. Ship roots use +Y as forward. Give tool emitters and asteroid hit targets named empty objects when they will be useful to game code.

Keep base models, removable equipment and presentation effects separate. The mining scene appends the actual starter-frigate asset from its source library, then creates a flight configuration and adds mining tools. It does not alter the outpost scene or replace its original ship.

Store each vignette under `Scenes/<SceneName>/` with:

- An editable Blender scene and a camera-rendered preview.
- A deterministic generation script and any local source library it needs.
- GLB exports for gameplay assets and separate effects.
- A manifest recording axes, units, geometry statistics and meaningful validation results.
- A README explaining rebuilding, importing and the intended activity.

Use standard metallic/roughness materials for GLB geometry. Procedural backgrounds, Blender lighting and compositor glow remain presentation elements. Recreate dynamic particles, bloom and beam behavior in the game engine. The current mining and combat vignettes are static visual setups; extraction rules, inventory, targeting and combat simulation are separate gameplay work.

## 9. Game preparation and review

Keep the Blender sources editable. The existing frigate uses a few thousand exported triangles, the cruiser about 5,300, and the station about 15,000. These are reference counts, not final budgets. Repeated small objects can create more draw calls than their triangle count suggests; combine, instance or atlas appropriate parts when preparing engine assets.

Before accepting a new scene, check:

1. The main activity reads clearly from the overview camera.
2. Ships retain the established silhouette, material roles and class proportions.
3. Solid geometry clears nearby assets; a landed ship fits its actual berth and surrounding structure.
4. Mining beams terminate on the rock surface and originate from attached tools; combat fire originates at weapon sockets and meets the intended shield surface.
5. Lighting preserves both the hull's form and the active contact points.
6. Source assets remain available, with effects and equipment organized separately.
7. The saved Blender file opens without missing external resources, and exports use the documented axes and scale.
8. The preview reflects the delivered scene. Record any new visual conventions here so the next scene can reuse them.
