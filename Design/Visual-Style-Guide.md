# Frontier Outpost — Visual Style Guide

Working art direction and environment design standards for the game's Blender scenes. This document describes the visual design language: how ships, stations, asteroids, materials, lighting and effects should work together.

## 1. Art direction

Build readable, stylized industrial space environments. Ships and stations should look assembled from practical machinery: armored pressure hulls, service spines, external equipment, clear docking surfaces and visible propulsion. Use restrained detail and strong silhouettes that remain readable from a tactical camera.

The established look combines blue-grey naval armor, graphite structure, pale titanium edges, orange identification bands, warm inhabited windows and blue propulsion. Space is dark blue rather than featureless black. Asteroids are irregular, angular masses with visible triangular facets.

Each scene should communicate one activity immediately: arriving at a station, landing, or extracting ore. Keep the subject, its destination and any working equipment visually connected.

## 2. Shape language and detail hierarchy

| Layer | Design pattern | Existing examples |
| --- | --- | --- |
| Primary forms | Simple boxes, tapered hulls, octagonal drums and large faceted rocks | Frigate keel, cruiser body, station hub, large foreground asteroids |
| Secondary forms | Segmented armor, bridge blocks, side pods, ribs and equipment mounts | Split dorsal armor, circular station pods, arm ribs |
| Tertiary detail | A few fasteners, vents, lights, short aerials and thin markings | Radiator fins, navigation lights, pad stripes |

Establish the primary silhouette before adding detail. Use dark recesses between armor plates to separate forms. Keep bevels small and mostly single-segment so edges catch light while preserving the angular style. Machinery should have a plausible attachment point. Repeated details should follow a structural rhythm rather than cover every surface.

Keep asteroid faces flat-shaded. Vary their proportions and vertex positions; distribute material values across facets. Reserve smoother geometry for circular rails, lights and selected manufactured parts.

## 3. Ship family and progression

Ships in the same family share split dorsal armor, a central service spine, orange bands, dark inset panels and twin drive pods. Change proportions and equipment as the class grows.

| Class | Visual identity | Role in the current scenes |
| --- | --- | --- |
| Starter frigate | Compact hull, short bridge, few armor sections, one small defense mount, limited equipment | Player's early ship; fits berth 07 |
| Aurora cruiser | Longer segmented hull, larger shoulders, extended spine and more exposed machinery | Preserved original ship; approximately four times the starter's length |

The cruiser remains a separate asset at its original scale. Future small ships should be designed around their role and landing envelope instead of uniformly shrinking the cruiser. Preserve the starter hull's recognizable silhouette when adding equipment.

Flight configurations retract landing gear and close its bays. Landed configurations deploy gear, idle the engines and align with the berth. Utility rigs should attach to explicit hardpoints and remain removable from the base ship.

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

Manufactured hull materials use moderate-to-high metallic values and visible roughness. Keep graphite parts distinguishable from the background. Use emission for active equipment, hot contact points, engines and windows.

## 6. Lighting and camera

Use cool starlight for the broad illumination, a restrained warm rim for industrial surfaces, and soft blue fill to preserve hull readability. Small amber and blue sources should illuminate their surroundings locally. Avoid lighting every surface equally: shadows help separate plates and machinery.

Use AgX and restrained fog glow in Blender. Bloom should reinforce hot points and beams without obscuring their geometry. Bright lights need a readable colored edge around the hottest core.

The shared tactical view is orthographic, at roughly 33 degrees of elevation. Keep +Z vertical and preserve a consistent sense of ship heading. The outpost overview uses a wide frame. Detail cameras can move closer while retaining the same viewing direction.

Compose clear foreground, action and background layers. Large cropped rocks can frame a corner; smaller distant rocks establish depth. Leave space around the ship silhouette and the activity's contact point. Background stars and nebulae should support the subject with quiet contrast.

## 7. Scene patterns

### Outpost and docking

Use a strong central station mass, secondary side pods and a clearly readable apron. Warm windows imply occupation. Navigation lamps and painted borders indicate where a ship can land. Place large vessels outside starter berths, with enough separation to make their class difference apparent. The starter ship can demonstrate actual berth fit.

Propulsion reads as a blue core with a tapered plume. A landed ship idles its drive; keep exhaust separate from the hull envelope.

## 8. Asset and scene structure

### Shared assembly conventions

Use named collections for ships, equipment, rocks, effects, backdrop, lighting and cameras. Keep each reusable assembly under an explicit `*_ROOT` pivot. Ship roots use +Y as forward. Give tool emitters and asteroid hit targets named empty objects when they will be useful to game code.

Keep base models, removable equipment and presentation effects separate. A new scene that reuses an existing ship appends it from its source library rather than copying or modifying it, so the original scene and asset stay intact.

Store each scene under `Design/<SceneName>/` (the outpost scene is `Design/FrontierOutpost/`) with:

- An editable Blender scene and a camera-rendered preview.
- A deterministic generation script and any local source library it needs.
- GLB exports for gameplay assets and separate effects.
- A manifest recording axes, units, geometry statistics and meaningful validation results.
- A README explaining rebuilding, importing and the intended activity.

Use standard metallic/roughness materials for GLB geometry. Procedural backgrounds, Blender lighting and compositor glow remain presentation elements. Recreate dynamic particles, bloom and exhaust in the game engine. The outpost scene is a static visual setup; docking, extraction, inventory, targeting and combat simulation are separate gameplay work.

## 9. Game preparation and review

Keep the Blender sources editable. The existing frigate uses a few thousand exported triangles, the cruiser about 5,300, and the station about 15,000. These are reference counts, not final budgets. Repeated small objects can create more draw calls than their triangle count suggests; combine, instance or atlas appropriate parts when preparing engine assets.

Before accepting a new scene, check:

1. The main activity reads clearly from the overview camera.
2. Ships retain the established silhouette, material roles and class proportions.
3. Solid geometry clears nearby assets; a landed ship fits its actual berth and surrounding structure.
4. Lighting preserves both the hull's form and the active contact points.
5. Source assets remain available, with effects and equipment organized separately.
6. The saved Blender file opens without missing external resources, and exports use the documented axes and scale.
7. The preview reflects the delivered scene. Record any new visual conventions here so the next scene can reuse them.

## 10. Station command HUD and four-berth apron

The home HUD uses a restrained Military C2 layout: a command header, resource strip, left station
services, right expedition briefing and a quiet utility rail. Graphite/naval panels, thin titanium
rules and square corners complement the assets. Keep typography legible and the central station
silhouette clear. Detailed market, processor, fitting and facility controls live in separate
service views. Actual catalog and roster data supply all values and available actions.

Keep the authoritative launch receipt beside the launch action: equipment that will fly, hold,
supplies due or carried, fittings at risk and missing equipment. Contract detail and crew
administration may scroll; commitment facts stay visible. A saved fitting is a recovery plan,
so distinguish it from equipment available for this launch and name its powered facility
prerequisite in visible text. Crew split drafts show their total separately from the applied
split. Quotes distinguish the shared contract pot from the pilot's conditional personal share.
Final expedition receipts remain available from Overview after their notices expire.

Overview names both the built and running tier, including OFF, and lists only currently enabled
options. Tier-II modules keep their owned silhouette when throttled or off; one or two small
service indicators show their effective tier. A closed socket has a removable graphite plate
with a titanium cross, visibly different from an available empty mounting deck. Affordable
development suggestions use banked goods and available power and open the existing facility
choices for inspection.

The resource strip holds what the pilot owns: credits and goods. Power is a capacity of the
station, not a good, so it shows in the overview and the facilities view and not in the strip.
The briefing draws the contract's site as the system map will (§11), framed on the site alone,
and lists its pockets with the map's glyphs. Berth and socket labels on the station are plain
ink on a thin rule: they name things and select nothing.

**Colour roles in the HUD.** These hold at home and in flight.

| Colour | Role |
| --- | --- |
| Orange | What is selected, and the action that commits: the launch, the jump, a chosen destination, the lock bracket |
| Cyan | Operational status and the pilot's own side: the connection, the pilot's ship, the dock |
| Yellow | A warning, with a `!` marker |
| Red | Danger and hostiles, with a `!!` marker or a shape of its own |
| Green | Something gained or in the pilot's favour, with a `✓` or `◆` marker |
| Plain ink | Readings and labels: resource values, headings, names |

No state rests on colour alone: each has a marker, a shape or a word as well (design §5). No text
is smaller than 11 px. Anything a pilot must press is at least 44 px, whatever its drawn size, and
nothing is reachable only by hover or a right click (design §5).

The forward apron has four side-by-side berths, 07–10, facing outward. The authored offsets,
deck height and painted boundaries live in `FrontierOutpost/berths.json`, shared by Blender,
the simulation and the client. Validate the full parked geometry, inter-ship clearance and
straight departure corridors. Socket A stays beside cargo; Socket B stays beside the apron,
clear of all four lanes. Crew preview instances in the Blender overview are presentation only;
the browser draws roster ships separately from the station asset.

Frame the home scene inside the actual gap between the service and briefing panels, using one
stable envelope for every supported facility. Preserve player zoom and orbit. Berth captions
sit beyond the ships' noses with a short leader; they must clear hulls and controls at both
1280×720 and 1920×1080. Landing gear uses the four authored berth positions.

## 11. Flight HUD and system map

In flight the screen belongs to the scene. The HUD keeps to five fixed places, so a pilot learns
once where each kind of thing is:

| Place | Holds |
| --- | --- |
| Top bar | The run's status, in place of the resource strip: phase, time, alert with the hostiles left, the contract's progress, the drives while something stops them, the relays while held, a scan in progress, a countdown to the next wave once a scan has found it |
| Top centre, under the bar | The locked target: name, distance, whether the weapons reach it, hull as a bar |
| Bottom left | The ship gauges: hull, hold, heat, one line each: a change just seen beside the value, and a short note on what happens next. The panel keeps its size; a note too long for its column is cut short, never wrapped |
| Bottom right | The minimap and the jump readout |
| Bottom centre, above the gauges' band | Notices, and under them the prompts |

The footer carries the full list of flight keys as a reference.

**Readings, not sentences.** Each cell of the run status is one reading with a short label. A
reading that needs attention changes colour and takes its marker; it does not move.
Use bounded, wrapping slots and reclaim branding space before removing tactical readings.
The full map sits between the target and ship-gauge bands; notices keep clear of its actions.

**Prompts.** A key is prompted only while pressing it would do something: the extractor in reach
of a deposit, the scan inside an unscanned site, the vent near the cut-out, the lock with a
hostile near, the trigger with one in range, the cruise drive on the legs. At most three show.
Instruments do not carry standing key hints.

**Notices.** A notice answers something the pilot did or warns of something coming. Its level
(news, warning, gain) shows by a marker and a coloured edge. The newest three stay together, each
for long enough to read; one said again replaces itself. They are announced politely to assistive
technology.

**No labels on the scene.** Places are not labelled where they are (design §2, §5). The minimap
gives their bearings, and the destination chosen on the system map is marked by one waypoint: at
the place when it is on screen, at the screen's edge toward it when it is not. The waypoint takes
no clicks, keeps clear of the HUD's panels, and goes once the ship is inside the place.

**The system map.** It opens over the scene (M, the minimap, or J with nothing chosen), and the
ship keeps flying under it, so it never takes the flight keys and it closes when the drive starts
to spool. Up on the chart is up on the screen with the camera at rest.

- Each kind of place has a glyph, used wherever the place is named: the chart, the list beside
  it, the briefing.
- A locked pocket has a dashed outline, a padlock and the word.
- The pilot's ship is a solid cyan arrow; a crew mate's is the same arrow in outline; a hostile
  is a red diamond, the nest core a red hexagon in outline.
- The destination chosen has an orange ring and a dashed course from the ship.
- The list beside the chart says everything the chart shows, and is what a keyboard, a touch or a
  screen reader works: the chart is never the only way to choose.
- What a scan found is tagged on the map and stays there; the notice that announces it goes.

Opening the map focuses its selected destination. Arrow keys and Home/End navigate destinations;
Enter selects, F6 cycles destination list, engage and close, and closing returns focus. Tab remains
the combat lock key, and movement, firing, digits, J and M stay live. Labels and hit targets use
displayed CSS pixels: at short heights the chart uses numbered destinations with full names and
facts in its adjacent list. Scan results persistently explain the optional relay task: two ships
within 8 m of the two pocket centres simultaneously for 30 seconds, for +50% of earned shared
contract payment. Selected relay feedback gives remaining range and held/waiting/linked state.

**The jump.** A jump is seen, by the pilot and by the crew. While the drive spools, two thin
rings close on the pilot's ship, one steadily and one in loops that quicken, so the last second
reads as the last. Where a ship leaves, a ring opens and a streak shoots out the way it went;
where it arrives, the streak comes in behind it and a ring closes on the hull. The effects are
the blue of propulsion, a bright core in a coloured edge (§6), and none is solid: nothing behind
one is hidden, and the spool is not taken for a shield. A ship that jumps is drawn where it was
and then where it is, never slid across the space between. The pilot's own jump also washes the
view once with the drive's light, to cover the camera's cut.

Motion is small. The wash is skipped when the system asks for reduced motion.

**Extraction.** The extractor's beam runs from under the ship's edge into the worked crystals:
a soft glow tinted by the material, around a bright, nearly white core (§6). Bands in the glow
run from the deposit toward the ship, and a few fragments in the material's colour break from
the crystals and fly into the hull, so cargo is seen arriving. Neither the beam nor the
fragments is solid. With reduced motion the beam holds still and the fragments are not drawn.

**Flight dust.** In flight a sparse field of dust lies just below the gameplay plane, fixed in
the world, so that open space shows the ship moving. Each speck is a faint, soft round dot, a
few pixels across, smaller and dimmer the deeper it lies; it never stretches into a streak,
which on the cruise drive read as lines across the view. Dust is told from stars by its soft
edge, by drifting against the sky as the ship moves, and by fading nearly out when the ship
stops. About sixteen cross the view at the starting zoom. It moves only as the scenery does, so
reduced motion leaves it as it is. The home scene has no dust.

Reliable crew events start and cancel spool effects; every remote spool also has a bounded
expiry. The pilot's confirmed jump returns camera focus to their ship without clearing a
valid hostile lock. Small material pulses follow actual hull loss or the own ship's shield loss,
never a cosmetic bolt reaching its endpoint; reduced motion disables these pulses.

The browser renderer uses AgX at exposure 1. Imported window and navigation emissions stay
bounded so warm windows retain their colour. Tactical marker materials bypass tone mapping to
preserve their specified colour roles. Keep space dark and armor lit; do not add bloom, shadows
or extra light rigs by default. Hostile fork silhouettes retain their gameplay footprint, with
restrained lit armor and recessed graphite details; beacon armor carries a small emissive cap.
