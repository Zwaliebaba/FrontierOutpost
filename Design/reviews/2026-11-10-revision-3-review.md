# Review of Frontier Outpost design, Revision 3

**10 November 2026 · Multi-agent review · Input to the Revision 4 draft**

Eight independent reviewers each read [Design/FrontierOutpost.md](../FrontierOutpost.md) at Revision 3 through one lens: MVP delivery, first session, freedom and world feel, long-term interest, mixed-experience play, loss and economy, technical feasibility, and visual readability. Their findings were merged into draft recommendations, a ninth reviewer attacked the draft, and the owner then took eight design decisions. This report records the result. The Revision 4 draft of the design applies it.

## 1. Verdict

Stage 1 as written in Revision 3 could not pass its own first gate. It builds one expedition, and two of the three example upgrades in §2 are production rates, so there is nothing for an outpost enhancement to change. Loss, crews, travel and the control model were undefined, which means the build could not start without someone inventing rules.

The three experience goals fare as follows, and the Revision 4 changes do not fully close any of them.

| Goal | Revision 3 | After Revision 4 |
|---|---|---|
| Feels like an open world | Conflicts with §1, which builds a home plus instanced expeditions. One expedition gives no choice of destination. | Freedom is restated as which opportunities to take and when to leave. Stage 1 gives no evidence of an open-world feel. |
| Interesting for a long time | Decisions stop being new between hours 10 and 30 (one reviewer's estimate; the document has no costs). | Seeded variation, threat tiers and bounded layouts give about ten runs of evidenced repeat play. Longevity is not tested. |
| Newcomers and veterans together | The mechanism (small-hull abilities) protects small ships, not new players, and the proof was deferred to Stage 2 behind four other new systems. | Crew rules exist and Stage 1 gathers paired-playtest evidence. A first-session newcomer still contributes only a second pilot. |

The biggest risk is cost. The recommendations add five to nine weeks by the reviewers' own figures, and the offsetting cuts are not quantified because no baseline estimate for Stage 1 exists. Section 4 sets this out.

## 2. Decisions taken by the owner

These eight decisions were put to the owner on 10 November 2026 and are applied in the Revision 4 draft.

| Decision | Choice | Alternatives declined |
|---|---|---|
| Sense of world in Stage 1 | One authored site with several pockets and seeded variation | A map of 3–5 sites (about 2 more weeks); one expedition as written |
| Cost of destruction | Cargo and upgraded modules are lost; the hull is reissued; a basic fitting is always available | Cargo only; hull, fitting and cargo |
| Control model | Direct steering with client prediction; lock-on weapons resolved by the server | Click-to-move with lock-on; direct steering with aimed shots |
| Hulls in Stage 1 | One hull with three role rigs | Three hulls; two hulls of different size |
| Crew capabilities | Access is shared across the crew; rigs stay with each pilot's own outpost | Host's outpost only; strictly per pilot |
| Player-to-player transfers | None in Stage 1 | Capped, logged hand-over; free gifting |
| Travel | Approach and return legs inside the expedition instance | Separate piloted transit; transition only |
| Upkeep | A flat supply cost per launch; wear and fuel in the data model but switched off | Light wear and fuel; no upkeep |

Two of these carry a consequence worth restating. Choosing one site means the open-world goal has no Stage 1 test. Choosing one hull means the §3 claim that small hulls keep unique value stays unproven until Stage 2.

## 3. Recommendations

The order is the order in which to act, following the challenger's objection that three recommendations each claimed to go first. Effort figures are single-reviewer estimates assuming one experienced engineer.

### 3.1 Build a client-only readability greybox first

- **Finding.** The gate "angled-view targeting stays readable" has no scene, measure or threshold, and art is already authored to an untested 33° view. At that angle a structure hides the plane for about 1.5 times its height behind it, and a raised ship is indistinguishable from a distant one.
- **Change.** §5 now requires one render height for combat ships, indicators on the plane, and allegiance silhouettes through occluders. §9 defines the measures. The greybox uses box proxies at manifest dimensions and the real station silhouette, with camera elevation and view width as parameters.
- **Scope.** Adds 1–2 weeks of client work. Needs no server.
- **Test.** Time to select a named hostile, wrong-target rate, heading and range judgement, repeated in greyscale and under simulated colour-vision deficiency.
- **Caveat.** The challenger noted that scripted motion cannot test targeting under direct control, so the greybox needs the player steering one ship.

### 3.2 Redefine the Stage 1 expedition so gate 1 can be tested

- **Finding.** Four lenses independently reached this. §9 builds "one 1–4-player expedition" and gates on "Outpost enhancements change expedition decisions". Of §2's examples, only "A sensor facility enables particular scans" changes a choice.
- **Change.** §2 now defines one authored site with three or four pockets that cannot all be cleared in one run, seeded variation, two or three threat tiers, finite yield and escalating threat. Every facility must open or change an expedition option. Unlocks are permanent, the active layout is bounded, and reconfiguring costs materials.
- **Scope.** Adds 1–2 weeks of content in one map. No new systems.
- **Test.** Among players with the same unlocks, do active layouts differ, and does layout predict pocket and rig choice on the same seed? The challenger showed that the simpler measure, "choice differs by outpost", passes by construction.

### 3.3 Cut breadth to pay for 3.2

- **Finding.** Stage 1 in Revision 3 implied three hulls, pilot progression, free placement, dynamic NPC prices, wear, fuel, site discovery and cross-session checkpoints. None is needed to test a Stage 1 gate.
- **Change.** §9 now has a Stage 1 in/out table. One hull with three rigs, progression through facilities only, fixed sockets, fixed posted prices, a flat launch cost, contract-supplied sites, and reconnection within a grace window.
- **Scope.** Removes content, by an amount no reviewer could quantify.
- **Trade-off.** The hangar of specialised ships and a working material economy are not evidenced in Stage 1. §7 now says so.

### 3.4 Write the Stage 1 specification and fix the data model

- **Finding.** No section of Revision 3 contains implementable rules, numbers or data. The scene generator has no construction sockets, and `station.glb` is one static assembly.
- **Change.** A new §10 lists the data tables required and the data-model rules: capability tags, site as template plus seed, versioned saves with a world identifier, item identities, and one ledger transfer for every stock change.
- **Scope.** About one week of design, plus 1–2 weeks to extend `create_scene.py` with socket empties and separate facility modules.
- **Test.** Adding a facility or a site parameter needs no code change. A conservation test checks that the ledger and the saves agree.

### 3.5 Define loss and a recovery floor

- **Finding.** §6 defined loss only for frontier assets, yet a Stage 1 gate is "players voluntarily repeat after loss". Recovery was circular: the only income is the expedition, which needs the fitting just lost. Fifteen minutes of objective play is about one whole session at §2's ratios.
- **Change.** §6 has a Stage 1 outcome table and a launch screen that shows the stake. §7 makes the basic fitting permanent and measures the 15 minutes in wall-clock time from the loss.
- **Scope.** Neutral; the rules are needed in any case.
- **Test.** A scripted run of three consecutive defeats from a fresh account, asserting time to refit. In playtests, minutes from defeat to next launch.

### 3.6 Add crew rules

- **Finding.** Revision 3 had no crew formation, host, cargo destination, payout or scaling rule. A veteran owns every rig, so a newcomer's only non-substitutable value is being in a second place at the same moment, which a second browser tab also supplies.
- **Change.** §2 has a Crews paragraph. §3 states the newcomer's value as simultaneity, requires input for every role, and adds an optional two-place objective.
- **Scope.** Small. Any co-op build needs these rules.
- **Test.** Paired playtests in three arms: veteran solo, veteran with newcomer, veteran with two clients. Compare the veteran's take per hour and record whether both choose another joint run.

### 3.7 State the control model and latency target

- **Finding.** "Direct player control" with no control scheme left the netcode cost open across a range of months.
- **Change.** §5 states direct steering with prediction, lock-on weapons resolved by the server, and a provisional latency target. "Validates movement, hits" became "clients send intents; the server simulates".
- **Scope.** About 4–6 weeks more than a command-based model and about 2–4 weeks less than aimed shots.
- **Test.** Playtests with 150 ms and 250 ms injected latency and 2% packet loss.

### 3.8 Make every gate able to fail

- **Finding.** Only travel and recovery had numbers, and an instant teleport satisfied the travel cap. The challenger showed that several proposed measures also pass by construction or are confounded.
- **Change.** §9 lists measures per gate, fixes the travel timer boundaries, and states what Stage 1 does not show.
- **Scope.** About one week of instrumentation.
- **Open.** Thresholds and cohort sizes are the owner's to set before playtests.

### 3.9 Tag the document by stage and restate the freedom promise

- **Finding.** §2 to §7 mixed Stage 2 and Stage 3 systems into present-tense text. §1's "Players choose outpost development, exploration, fighting, or a mixture each session" was not true in Stage 1.
- **Change.** Paragraphs carry stage tags. §1 states freedom as choice with consequences and says it is not a seamless open world.
- **Scope.** Neutral.

### 3.10 Extend the Visual Style Guide

- **Finding.** The style guide covers Blender scenes and says "targeting and combat simulation are separate gameplay work". It has no rules for allegiance, projectiles, minimum on-screen size, zoom bands, construction sockets or interface, and its colour roles reserve nothing for gameplay state.
- **Change.** None made; the style guide was outside this review's edit scope.
- **Recommended additions.** A tactical readability section, a socket and module convention, zoom bands in pixels per metre, reserved state colours with a second channel, and a plane datum for ship pivots.

## 4. Cost

The draft recommendations claimed to be mostly specification paid for by cuts. The challenger tested that against the reviewers' own figures and it did not hold.

| Item | Weeks |
|---|---|
| Expedition content (3.2) | +1 to 2 |
| Gate instrumentation (3.8) | +1 |
| Specification and facility assets (3.4) | +1 to 2 |
| Readability greybox and generator work (3.1, 3.4) | +2 to 4 |
| **Stated additions** | **+5 to 9** |
| Cuts (3.3) | Not quantified |
| Crew rules, loss rules, ledger, identity | Not priced |

The additions are in weeks and the cuts are in fractions, so the ledger cannot close until Stage 1 has a baseline estimate. Producing that estimate is the first use of the §10 specification.

## 5. Decisions still open

These are marked **Open** in the Revision 4 draft or sit outside the design document.

1. **Relaunch after destruction.** May a destroyed pilot rejoin the live instance once in the basic fitting? It keeps a crew session alive, softens the loss that gate 3 measures, and needs a mid-instance join path.
2. **Gate thresholds and cohort size** for each of the four Stage 1 gates.
3. **Content horizon.** With one hull and about six facility purchases, a "veteran" is someone who knows the map. Decide how many hours Stage 1 should hold before it is content-complete.
4. **View elevation.** Whether the 33° angle may change if the greybox favours a steeper one. This touches AGENTS.md §4.5 and the style guide.
5. **Production timing.** Per completed run, or by elapsed time. Elapsed time needs wall-clock catch-up in a tick-only simulation and creates an idle income source.
6. **Extraction greybox.** Whether to prototype the heat and yield loop client-side before the server exists. It gives early evidence on "Is active extraction engaging rather than repetitive labor?" but the rules would be written outside `frontier-sim` and thrown away.
7. **AGENTS.md §8 decisions**, which do not belong in the design document. Reviewers suggest this order and these starting points:
   - Runtime and transport together: WebSocket with JSON behind a transport interface.
   - Identity, which §8 does not list: a guest account with a device-held secret.
   - Persistence: embedded SQLite with versioned per-account documents and an append-only ledger.
   - Licence: before any external tester sees the build.
8. **Save continuity.** Whether Stage 1 saves must carry into Stage 2 or may be wiped.
9. **Playtest audience.** Internal or public; this sets the timing for accounts, hosting and the licence.
10. **Determinism scope.** The design never uses replay. One reviewer suggests scoping AGENTS.md §4.2 to "same build, seed and inputs give the same state hash on Linux and Windows".

## 6. Proposed build order

1. Readability greybox in the client (3.1).
2. Stage 1 specification and loss rules (3.4, 3.5), which also yields a baseline estimate.
3. Simulation: movement, one weapon, two NPC types, an extraction node and cargo on fixed ticks, with a replay test.
4. Runtime and transport; a single-player session over the wire.
5. Co-op for 1–4 by invite code; destruction, loss and return.
6. Outpost: three sockets, three facilities under a power cap, each opening a different pocket; fixed-price NPC trade; ledger events.
7. Accounts and persistence for the home and its stock.
8. Phase timers and a gate dashboard.
9. Playtest cohort against the thresholds.

## 7. Considered and not adopted

| Proposal | Source | Outcome |
|---|---|---|
| Map of 3–5 sites | Delivery, challenger | Declined by the owner. It was the only item that would test an open-world feel. |
| Persistent chart of revealed pockets | Freedom | Deferred. It conflicts with seeded variation unless the template is fixed. |
| Offline production queue | Long-term | Deferred, pending the production-timing decision. |
| Capped gifting between players | Loss and economy | Declined by the owner. |
| Occluder height cap in the combat area | Visual readability | Not adopted. The challenger noted it passes the readability gate by removing the structures; apply it only if the greybox fails. |
| "Not a seamless open world before Stage 4" | Draft | Reworded. The design never promises one; Stage 4 is a possibility, not a commitment. |

## 8. Verification

The orchestrating session checked the quoted evidence behind every high-severity finding against the design document, and checked these repository claims directly:

- Style guide quotations at lines 36, 40, 46, 72, 74 and 100.
- `Design/FrontierOutpost/README.md`: "The GLB exports contain no gameplay scripts, collision shapes or baked lightmaps."
- `create_scene.py` contains no construction sockets or facility objects.
- The source is about 290 lines across `crates/` and `client/src/`, and the server binary only prints a line.
- Both preview PNGs are 132-byte Git LFS pointers in this checkout, so no reviewer saw an image.

The following rest on a single reviewer's judgement and were not checked:

- All effort estimates in weeks.
- The hour-by-hour estimate of when decisions stop being new.
- Occlusion and on-screen pixel arithmetic, and the claim that scene assets sit off the gameplay plane.
- Snapshot size and bandwidth estimates.
- The claim that internally tagged serde enums rule out non-self-describing binary formats, and the state of WebTransport support.
- Every claim about fun. All reviewers read a document; none played a build.

## Appendix: findings by lens

**MVP delivery.** Gate 1 cannot be passed by the build §9 describes. No section contains implementable rules or data. Stage 1 loss is undefined. Two gates have no threshold and one passes trivially. The control model is unspecified and the §8 decisions have a forced order. §2 to §7 mix later-stage systems into present-tense text. Stage 1 is over-scoped for a first gate test. "Visible facility upgrades" has no asset path.

**First session.** The 15-minute recovery target does not fit a first-run loss. Nothing guarantees the signature moment, a facility that transforms the outpost, in session one. About twenty named concepts precede the first launch and about five are needed. "Choose a new objective" has one answer. The solo newcomer carries the heaviest load. Travel uses its whole ceiling and has no stated content.

**Freedom and world feel.** One expedition leaves no destination choice. Travel is budgeted but not designed. The owner's goal and the document disagree. "When to leave" has no stated pressure. Nothing discovered persists in Stage 1. The cheapest sources of felt openness, in order, are leaving under rising risk, pockets that cannot all be taken, and a visible undock from the outpost view.

**Long-term interest.** Gate 1 is untestable as written. The repeat gate has no horizon. Specialisation has no respecialisation rule and no end state. The 16-minute objective rests on one sentence. Net progression per session cannot be computed. A veteran has nothing to aim at once the hangar is complete. Absence is safe, but nothing pulls the player back.

**Mixed-experience play.** Crew rules are absent. Proceeds and transfers are undefined, and either answer breaks something. A finite pool makes each extra crew member a cost. Hull capabilities protect small ships, not new players. The mixed-crew proof is deferred to a stage with four other new systems. Nothing implements the multiboxing sentence. A newcomer's death can end the shared session.

**Loss and economy.** Stage 1 never says what a defeat costs. Recovery is circular and the floor after three losses is undefined. The loss gate is unmeasurable and §7's metric list is for Stage 2. Stage 1 trading contains no decision. Wear reads as a tax. Logout rules are over-built for Stage 1. Alt-account feeding, crafting arbitrage and combat-state resets need Stage 1 rules.

**Technical feasibility.** Three sentences in §5 imply the expensive netcode. The §6 checkpoint rules require serialising live instances. Persistence and identity are implied but undefined. Determinism is an engineering preference the design never uses. JSON over WebSocket is sufficient for Stage 1 and reversible. Instrumentation is cheap only if built as a ledger first. "Visiting crews" and offline production hide a networked, time-driven home.

**Visual readability.** The readability gate cannot be failed as written. Nothing anchors ships to the plane. Occlusion is larger than "fade or outline" implies, and the two documents pull in opposite directions. There is no combat visual language. The signature is not buildable from the current scene. Scale and zoom are undefined. The interface is entirely unspecified. Colour roles are overloaded.

**Challenger.** The cost claim failed on the draft's own figures. Proposed measures for gates 1 and 3 passed by construction. A first-session newcomer with one empty socket adds no capability. In-expedition salvage hand-over was an uncapped gifting channel. Turning wear and fuel off left no material sink. The draft relabelled the gap on all three experience goals instead of evidencing them. No Stage 2 system was pulled into Stage 1.
