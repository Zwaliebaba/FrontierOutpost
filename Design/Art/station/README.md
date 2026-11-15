# M9 station art study

Editable exterior and interior sources inspired by the owner's ring-station reference:
blue-grey segmented armour, a broad inhabited ring, a heavy command hub, warm windows,
a single illuminated hangar, a stepped command tower and an underslung industrial core.
The pipeline exports both assets and the catalogue registers them for later M9 integration.

The game draws both (plan 9.6): the client looks them up in the catalogue in place of the
legacy station, at home and at the dock. The rules own the station's geometry
(`crates/sim/src/station.rs`) and send it with the site; the client stands the exterior at the
dock, its mouth facing the site's centre, with each installed facility's tell on its socket's
hardpoint, and swaps in the interior when the station is selected at home, where modules are
built. M9.1's greybox readability measurements and owner review may still change the scale.
Existing M8 tells are reused.

## Detailed reference pass

The owner supplied [this detailed station reference](https://chatgpt.com/s/m_6af9885a13a48191a1e194da80060ce5)
after the initial study. The revised exterior adds framed facade windows on both sides of the
ring, recessed louvres, chamfered armour, orange service stencils, radial bridges, stepped hub
belts and a substantial command tower with amber vertical spines. The hangar gains segmented
cheeks, recessed portal lighting, interior ribs and ceiling light strips, plus a marked transit
threshold. Its approach envelope remains clear; the threshold finishes at or below Z=0.

Facility-specific shapes remain in the existing facility tells. The reference's decorative
pads are not authored as exterior berths or parked ships. The interior receives matching
corridor decks, floor seams, lane paint and cargo/processor details, without raising geometry
into socket or ship envelopes. Tell mounts now sit above the armour panels; the outer mounts
and reactor hardpoint are moved outward so all four pass visibility checks in both reviewed
station orientations. The shared material library and contract are unchanged.

`refine_station.py` applies this pass to the initial sources once and refuses to apply twice.
It checks each file before saving, including both assets against the proposed reversed world
orientation. Subsequent modelling can be done directly in the Blender files.

## Camera and layout

- All review renders are orthographic. The tactical render uses the contract's exact
  `(0.43, -0.72, 0.545)` direction (approximately 33 degrees elevation), without changing
  the game's camera. The entrance render uses the opposite Y azimuth at the same elevation. A fourth render,
  `station-camera-facing.png`, keeps the exact resting camera and proposes a 180-degree
  world rotation of the entire station instead. The exported local axes are unchanged.
- The ring's proposed diameter is 32.4 metres, approximately 2.2 times the old station's
  width. Ships and facilities retain their authored size. `contract.json` remains unchanged;
  its final station scale is still unset pending M9.1.
- The reference's long spires are shortened. Most of its vertical mass is below the flight
  plane so the command crown does not obscure as much traffic.
- The pipeline requires a +Y-facing mouth; the resting camera is on the -Y side. Its frontal
  opening is consequently on the far side in the tactical render. Broad roof ribs and orange
  service bands identify the hangar from above. The entrance render is an orbit review, not
  the default gameplay view. The proposed world rotation brings the mouth into the default view and also passes the
  interior projection check (using the equivalent opposite local view direction). The game
  does not use that rotation: it turns the station by the rules, the mouth toward the site's
  centre, so which side faces the resting camera depends on the site. The client places the
  root at the rules' station centre, its deck (Z=0) half a hull below the flight plane.
- `Hangar_Mouth` is at `(0, 14, 0)`, with an 8 by 3.5 metre opening and the contract's 30 metre
  straight approach. No exterior mesh or largest supported tell intersects that lane.
- `Tell_A`, `Tell_B`, `Tell_C` and `Tell_reactor` are hull-mounted anchors. Facilities are not
  baked into the exterior. The large review renders temporarily place three existing tells.
- The cutaway shares the ring footprint and is drawn with the exterior's transform, in its
  place. Socket A is beside cargo, B beside the hangar deck,
  and C has separate `Closure_C*` meshes. The client gathers them under `Socket_C` and lays a
  copy on any other closed socket (B before the first facility). The processor has a hopper, furnace and output chute.
  `Reactor_Upgrade*` meshes are separate for M10. `Parking_frigate` retains the current hull's
  clearance. Future hulls will require their own parking places in M11.
- Checks use all three full 5 metre socket envelopes simultaneously, even though the populated
  review uses current tier-II modules. Their projected bounds and the frigate parking envelope
  do not overlap from the resting camera. The populated review shows C open and the reactor
  visible to illustrate the later upgraded layout, not the initial gameplay state.

## Rebuild and inspect

The sources remain editable; no regeneration is required to work on them. Keep the surrounding
`Design/Art/library` folder so relative material links resolve.

`model_station.py` is a one-time authoring aid, like the crew-quarters example. It creates both
sources and refuses to overwrite either. `frontier_art.py` remains the only exporter.

```sh
blender -b Design/Art/station/exterior/exterior.blend --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- check
blender -b Design/Art/station/interior/interior.blend --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- check
blender -b Design/Art/station/exterior/exterior.blend --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- export
blender -b Design/Art/station/interior/interior.blend --python-exit-code 1 --python Design/Art/tools/frontier_art.py -- export
python3 Design/Art/tools/catalogue.py
blender -b --python-exit-code 1 --python Design/Art/tools/preview_station.py -- /tmp/m9-review
```

`preview_station.py` creates large tactical, entrance, camera-facing and populated-interior renders. It imports
existing exported ships and facilities for scale and adds presentation lighting and restrained
glow. These additions never enter either source or gameplay GLB. A warm area light illuminates
the hangar in review renders only; the GLB carries emissive geometry, and runtime lighting
still needs to be implemented as part of M9 integration. The standard 512-pixel
pipeline previews contain only the asset being exported, under the shared preview lighting.

## Validation

Verified in Blender 4.3.2:

- Exterior: 25,384 evaluated/exported triangles; interior: 2,604. Both are below 30,000.
- Both saved sources pass the pipeline, including linked materials, runtime nodes, hangar
  opening, approach lane, tell hardpoints, full socket envelopes and tactical projection checks.
- Reimporting each GLB preserves triangle counts and bounds (within 0.0001 metres), with all
  manifest anchors retained.
- Deliberately narrowing the actual mouth or blocking its approach fails validation; moving
  interior socket B onto socket A fails the projection check. These mutations were not saved.
- All 16 Blender pipeline tests and eight Blender-free export verifier tests pass.
- Export hashes and catalogue freshness pass. Blender 5 has not been tested here.

Art checks establish geometric clearance, not gameplay targeting or frame-time gates. The
interactive M9.1 and M9.7 measurements remain outstanding.
