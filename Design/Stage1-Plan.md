# Stage 1 implementation plan

**Draft 1 · 10 November 2026 · For owner review**

This plan turns Stage 1 of the [game design](FrontierOutpost.md) (§9, with the values in §10) into
an order of work. It follows the decisions recorded in [AGENTS.md](../AGENTS.md) §8. Where the
plan and the design disagree, the design wins and the plan is corrected.

## 1. How the work is cut

The owner directs and reviews; AI coding agents write most of the code. So the unit of work is a
**work item**: one change an agent can finish in a session and the owner can review as one pull
request. Every work item names the check that proves it. A work item that cannot name one is too
big or too vague and is split first.

Each milestone ends in something the owner can run and judge, not in a layer of code. The order
puts the riskiest questions first where the owner's decisions allow it.

## 2. Decisions the plan rests on

| Area | Decision |
| --- | --- |
| Runtime and HTTP | Tokio; axum inside the game server serves the built client |
| Transport | WebTransport on quinn and `web-transport-quinn`; no fallback |
| Wire format | JSON on reliable streams; snapshots as binary datagrams of at most 1,000 bytes |
| Prediction | `frontier-sim` movement compiled to WebAssembly and run in the browser |
| Identity | Passkeys |
| Persistence | Embedded SQLite with an append-only ledger; Stage 1 data may be wiped |
| Simulation | Fixed ticks, no I/O; sites reproducible from a seed; no session replay |
| Camera and input | The greybox defaults; mouse and keyboard; touch after the Stage 1 gates |
| Art | Sockets and facility modules authored through `create_scene.py`; from M8 every asset modelled by hand through the art pipeline (`Design/Art/`) |
| Playtests | About eight invited testers, on machines the owner provides |
| Licence | All rights reserved for now |

## 3. Milestones

### M0 · Foundations

*At the end: the real server and client exchange messages over WebTransport, in CI.*

| # | Work item | Proof |
| --- | --- | --- |
| 0.1 | Add Tokio, quinn, `web-transport-quinn` (with the `ring` backend) and axum to the workspace | The workspace builds and passes clippy on Linux and Windows CI |
| 0.2 | Server: accept a WebTransport session with a certificate generated at start-up; axum serves the client and the certificate hash | A Rust client test connects and completes the existing Hello/Welcome handshake |
| 0.3 | Client: a `net` module that connects, sends and receives JSON messages on streams, and exposes a datagram channel | A browser test in CI completes the handshake against the real server |
| 0.4 | Snapshot codec in `frontier-protocol`: binary encode and decode in Rust, decode in TypeScript, from shared test vectors | Round-trip tests in both languages; a test fails if 4 ships and 24 NPCs exceed 1,000 bytes |
| 0.5 | WebAssembly build of `frontier-sim`, loaded by the client, in CI | The client calls a `frontier-sim` function in a browser test |
| 0.6 | ~~Run the transport spike in Firefox and Safari~~ Dropped by the owner; the spike is deleted | Firefox and Safari stay untested, an accepted risk |

### M1 · Fly

*At the end: you steer a ship on the real server, and it stays responsive with delay and loss
injected.*

| # | Work item | Proof |
| --- | --- | --- |
| 1.1 | `frontier-sim`: world state, ship movement by thrust and turn, fixed 20 per second tick | Unit tests for movement, limits and tick stepping |
| 1.2 | Server: an expedition instance that takes intents and sends snapshots; any number of clients up to four from the start | An integration test with two clients sees both ships move |
| 1.3 | Client: draw ships from snapshots, interpolated 100 ms behind, with the greybox camera, look, zoom and picking promoted into the client | Vitest for interpolation; the greybox still passes its tests |
| 1.4 | Client: predict the player's own ship with the WebAssembly rules and reconcile against snapshots | A test replays recorded snapshots with delay and asserts a bounded correction |
| 1.5 | A development switch that routes traffic through a delay-and-loss relay | The owner flies at 150 ms and 250 ms round trip and records the result |

### M2 · Fight

*At the end: you lock, shoot and are shot by NPCs, and can be destroyed.*

| # | Work item | Proof |
| --- | --- | --- |
| 2.1 | Selection that locks: click a ship, cycle to the nearest hostile, centre the camera when not steering | Vitest for selection state; a browser test locks a target |
| 2.2 | Weapons: lock-on fire, hits resolved by the server, projectiles sent as events and drawn by the client | Sim tests for range, cooldown and damage |
| 2.3 | NPCs: drone, raider and warden behaviours from §10 | Sim tests for each behaviour's target choice |
| 2.4 | Destruction and its outcome: cargo and upgraded modules lost, hull reissued, pilot returned home | Sim test of the §6 outcome table |
| 2.5 | Readability rules in the real client: one render height, allegiance by shape and colour, silhouettes through structures | The greybox's measures run against the real renderer |
| 2.6 | Benchmark at the provisional caps of 24 NPCs and 64 projectiles | A recorded frame-time and snapshot-size result |
| 2.7 | Collision, added by the owner: ships push each other apart and slide off asteroids, with no damage; asteroids move from client decoration into the server's world, sent on joining; prediction collides with them | Sim tests that ships never overlap each other or an obstacle |

### M3 · Extract

*At the end: a solo pilot can complete a tier I run from launch to dock. This is the first build
in which the game's loop can be judged.*

| # | Work item | Proof |
| --- | --- | --- |
| 3.1 | The site as data: template, pockets, seed parameters, threat tiers | The same seed gives the same site in a test; a second seed differs |
| 3.2 | Extraction: rates, heat, cut-out and manual venting; nothing yields without input | Sim tests, including that an unattended extractor stops |
| 3.3 | Alert, waves and escalation with time and heat; return-leg interception by cargo carried | Sim tests against the §10 tables |
| 3.4 | Cargo, the approach and return legs, docking banks cargo, the instance ends | An integration test completes a scripted run |
| 3.5 | Scanning reveals the hidden seed values | Sim test; the client shows what was revealed |
| 3.6 | Phase timers for preparation, travel, objective and unloading | The timers appear in the run's event log |

**Checkpoint.** The owner plays the loop before M4 starts. The review's largest risk was that
extraction is labour, and the owner chose not to prototype it separately, so this is where that
risk is first seen. If the loop is dull, it is reworked here, before accounts and the outpost are
built on it.

### M4 · Persist

*At the end: you sign in, and your home survives a server restart.*

| # | Work item | Proof |
| --- | --- | --- |
| 4.1 | SQLite storage: versioned account documents with a world identifier | A test loads a document written by the previous schema version |
| 4.2 | The ledger: every change to stock goes through one transfer function and is written in the same transaction as the save | A conservation test: the ledger folded equals the stock saved |
| 4.3 | Passkey registration and sign-in, carried over a stream; a session token for reconnection | A browser test registers and signs in with a virtual authenticator |
| 4.4 | Reconnection within the grace window; the disconnect exposure timer; pocket checkpoints restored after a server restart | An integration test kills and restarts the server mid-run |
| 4.5 | A command that wipes Stage 1 data | A test runs it and finds an empty world |

### M5 · Outpost

*At the end: a completed run buys a facility that visibly changes the outpost and opens something
new on the next run. This is the game's signature.*

| # | Work item | Proof |
| --- | --- | --- |
| 5.1 | `create_scene.py`: construction sockets and three facility modules at two tiers, exported separately | The manifest lists them; the owner runs Blender and commits the regenerated assets |
| 5.2 | Client loads the station and module models; CI fetches the real asset files | A browser test finds each socket and module in the loaded scene |
| 5.3 | Outpost rules as data: facilities, capability tags, power cap, sockets, adjacency, reinstallation cost | Sim tests; a test adds a facility through data alone |
| 5.4 | Outpost view: free orbit, select a facility, build, upgrade and reconfigure, with previews that name what opens | Browser tests for build and reconfigure |
| 5.5 | Capabilities reach the expedition: pockets, rigs, modules and threat tiers follow the powered layout and unlocks | An integration test shows a locked pocket opening after a purchase |
| 5.6 | Trade and processing: fixed NPC prices, recipes that advance by the server clock, online or away, the launch cost | Sim tests; a data test that no recipe is an arbitrage |
| 5.7 | Fittings and the launch screen, showing the stake at replacement price | Browser test of the stake shown |

### M6 · Crew

*At the end: two to four players run an expedition together under the crew rules.*

| # | Work item | Proof |
| --- | --- | --- |
| 6.1 | Invite code, lobby, the host chooses contract and tier | An integration test with two accounts joins by code |
| 6.2 | Shared access, threat and yield scaling, the payout pot and its split | Sim tests against the §10 crew table |
| 6.3 | The two-ship relay objective | Sim test |
| 6.4 | A destroyed pilot returns home while the crew continues; a disconnected pilot's run is abandoned if the instance ends | Integration tests for both |

### M7 · Measure

*At the end: every Stage 1 gate is met, failed or recorded as unproven, from evidence the owner
can produce alone.*

There are no testers, so the owner closes M7 alone. A designer playing their own game can fail a
gate but can rarely pass one: they know the rules, they want the game to work, and they will come
back to it whatever happens. So M7 measures what one person can measure honestly, checks against
the rules what can be checked without players, and records the rest as unproven instead of
lowering the bar. Each measure carries its basis:

- **Solo:** the owner's own play. It counts in full where the measure comes from the game's
  structure, such as time spent travelling. Where the owner's expertise flatters the figure, a
  miss is a failure and a pass is recorded as *met by the owner only*.
- **Rules:** computed from `frontier-sim` without anyone playing.
- **Unproven:** needs players other than the owner. It is neither met nor failed.

| Gate (design §9) | Basis in M7 | What M7 can show |
| --- | --- | --- |
| 1. Outpost changes decisions | Rules; solo | Whether a facility changes the best pocket or rig among those open either way. Layouts compared across players stay unproven |
| 2. Targeting stays readable | Solo, in all three vision modes | A failure, or a pass by the owner only |
| 3. Players repeat after loss | None | Unproven: no measure of one's own return means anything |
| 4. Travel and recovery | Solo | Travel share and minutes to recover, in full. Time to the first decision is a failure or a pass by the owner only |
| Crew evidence (not a gate) | Solo | The owner on two clients; the newcomer pairing stays unproven |

| # | Work item | Proof |
| --- | --- | --- |
| 7.1 | Gate events: selections, losses and relaunches, layouts and reconfigurations, choices per seed | Each §9 measure can be computed from one recorded session |
| 7.2 | A report that computes the gate measures from the database | It reproduces hand-checked figures from a fixture |
| 7.3 | The report states each measure's basis and how many players it rests on, and marks a measure that needs other players *unproven* when the saves hold one, never met | A report test on a one-pilot fixture shows gates 1 and 3 unproven, not met |
| 7.4 | A rules check for gate 1: for each seed in the playtest pool and each layout of the three facilities, the pocket and rig with the best expected take, counting only those open under both layouts compared, so an unlock alone does not count | A sim test on a fixture where a facility must change the best choice; the check's table for the pool |
| 7.5 | Solo sessions: from a wiped world on a fresh account, the owner plays the playtest script's four sessions (about ten runs) on the seed pool, runs the greybox block in normal, grey and one colour-deficient mode, and flies one crew run on two clients | The report from those saves and greybox files |
| 7.6 | The write-up: each gate met, failed or unproven, with its basis | The write-up; design §9 records the outcome |

**Closing M7.** The owner confirms the provisional thresholds before 7.5. M7 closes when the
write-up exists. A failed gate is fixed and measured again before Stage 1 is called done; an
unproven gate does not hold M7 open.

**What stays open.** Gates 1 and 3 cannot be met without other players, and Stage 1 exists to
prove them. The owner has decided that Stage 2 may start with them unproven (AGENTS.md §8),
accepting the risk that the outpost does not change other players' decisions, or that other
players do not come back after a loss, and that Stage 2 is built on a loop nobody else has tried.
They stay unproven in every report until they are measured with players other than the owner,
using the [playtest script](Playtest-Script.md), whenever there are any. Testers reached over the
internet would reopen the hosting decision and the deferred hardening (AGENTS.md §8).

### The Stage 1 expansion: M8 to M12

The owner has no testers, so after M7 the game is expanded for one player instead of moving to
Stage 2 (design §11). M7 measures Stage 1 as §10 specifies it, before any of this is built, so
the expansion is chosen with M7's results in hand and the measurements describe the game they
were taken on. The wingman comes first: it needs one facility module and no hull. The station is
rebuilt around its hangar next, before anything else is authored at its old scale: socket C and
tier 3 are built on the new station, and the berth limit, which would constrain the new hulls,
is gone before they are made. The sites, which need the most authored content, come last. Each
milestone brings its own art, modelled by the owner through the art pipeline that M8 builds
first, so none waits on another's Blender work.

### M8 · Art pipeline and wingman

*At the end: art goes from the owner's Blender files into the game through one pipeline, and you
launch with a wingman, order it to hold a relay, and earn the relay bonus alone.*

The pipeline comes first because every later milestone's art goes through it, and the crew
quarters are its first new model.

| # | Work item | Proof |
| --- | --- | --- |
| 8.1 | The art pipeline's Blender tool, `frontier_art.py`, with `new`, `check` and `export` for every kind, `contract.json` and the shared material library, per the [art pipeline](Art/README.md) | The owner runs it on fixture files: it passes a good one and fails one that breaks each rule |
| 8.2 | The catalogue, assembled without Blender; CI's art check (each manifest's GLB and hash, every facility's tiers and tells unless `art: pending`, names and budgets); the client loading art only through the catalogue, each facility when first shown | CI fails on a missing tier and on a GLB that differs from its manifest; a browser test loads a module through the catalogue |
| 8.3 | The six generated facility modules split into their own facility files (`split`), exported through the pipeline, and removed from `create_scene.py` | The six pass `check`; the outpost's browser tests still find every module |
| 8.4 | The owner models the crew quarters at two tiers, with their tells | `check` passes and CI's art check is green |
| 8.5 | Rules: crew quarters as data; a wingman is a ship that counts in threat and yield scaling but not in the pot; pilots and wingmen never exceed four ships | Sim tests against design §11, including the four-ship limit |
| 8.6 | Wingman behaviour: follow, hold and return; extract at crew quarters 2; it acts only on what a pilot could see | A sim test for each order, and one that a wingman never acts on an unscanned seed value |
| 8.7 | Server and protocol: a wingman hired at launch for the launch cost, with an instrumentation event; orders as intents; wingmen in snapshots | An integration test: a solo pilot and a wingman earn the relay bonus |
| 8.8 | Client: the launch screen offers the wingman; order keys and a mark on the plane; the HUD shows its order and state; it is drawn as an ally, told from a pilot | A browser test gives each tier 1 order |
| 8.9 | Loss and cargo: a destroyed wingman loses its cargo and fitting; a docked wingman's cargo banks to its pilot | A sim test of both outcomes |

**Checkpoint.** The owner plays solo with a wingman. If the scaled threat and yield make runs
trivial, or the wingman too often feels like a burden, its values are reworked before M9.

### M9 · Station exterior and interior

*At the end: the station is two to three times larger and shows each facility's tell outside;
you select it to see your modules and ships inside, fly out of its hangar at launch and back in
to dock, and the berths are gone.*

The owner models the exterior, the interior and the tells by hand, through the art pipeline
(M8).

| # | Work item | Proof |
| --- | --- | --- |
| 9.1 | The larger exterior's silhouette and hangar mouth in the readability greybox, before the owner models it | The greybox block's measures with the new silhouette at the dock, against gate 2's thresholds |
| 9.2 | The owner models the exterior through the pipeline: the hangar mouth against the largest hull, a clear approach lane, a tell hardpoint per facility and for the reactor, within the scale the greybox chose and the budget | `check` passes; the tool fails it with the mouth narrowed or the lane blocked |
| 9.3 | The owner models the interior through the pipeline (the hangar deck with a parking place per hull, sockets A to C with C closed, the processor and the cargo bay), and a tell at each tier in each existing facility's file | `check` passes for the interior and every facility; CI's art check is green |
| 9.4 | Rules: docking and undocking measured from the hangar mouth; crew ships leave the mouth one after another; jump arrivals stay outside the station; the berth slots removed | Sim tests, including four ships launching without colliding and a jump home landing clear of the station |
| 9.5 | Server and protocol: berth slots leave the crew roster and the wire | An integration test: four pilots launch and dock |
| 9.6 | Client: the exterior at the dock and at home with its tells drawn from facility state; selecting the station opens the interior in the same camera, with the owned ships, the modules and their states; building and upgrading move into the interior; ships leave the mouth at launch and are drawn into it on docking; the station HUD without berth slots | Browser tests: open the interior, build in a socket and see its tell outside, launch and dock |
| 9.7 | Gates 2 and 4 measured again with the new station, as an addendum to M7's write-up | The addendum |

**Checkpoint.** The owner builds a facility inside, sees its tell outside, launches and docks.
If the exterior hides ships or crowds the dock, or the interior does not read as the outpost
changing, it is reworked before M10 builds on it.

### M10 · Deeper outpost

*At the end: a third socket and a third tier change the outpost, inside and out, and force
choices between six facilities.*

| # | Work item | Proof |
| --- | --- | --- |
| 10.1 | The owner models the reactor in the interior file and its tell, and the three Stage 1 facilities' tier 3 with their tells, through the pipeline | `check` passes, including every module within the socket envelope; CI's art check is green |
| 10.2 | Rules as data: the reactor and power cap 8, socket C and its adjacency, tier 3, fuel cells with their recipe and prices | Sim tests; the data tests of design §11's checks, including no arbitrage |
| 10.3 | Interior view: build the reactor, socket C and tier 3, with previews that name what opens; the tells follow outside | A browser test builds in socket C and sees the reactor's tell |
| 10.4 | Tier 3 reaches the expedition: preparation shows the rich pocket and waves; armour plating | An integration test for each |
| 10.5 | A cost test of the content horizon across design §10 and §11 | It shows about thirty solo runs to buy everything |

### M11 · Ships and fittings

*At the end: you choose a hauler or a gunship at launch, and losing one costs you its
replacement.*

| # | Work item | Proof |
| --- | --- | --- |
| 11.1 | The owner models the shipyard at two tiers with its tells, and the hauler and gunship, through the pipeline; the starter frigate moves from the generated scene into its own hull file | `check` passes, including each hull through the mouth and in its parking place; CI's art check is green |
| 11.2 | Hulls as data: cargo, handling, weapons, shield, slots, collision radius and replacement price; the afterburner and armour plating | Sim tests per hull; prediction uses the same rules in WebAssembly |
| 11.3 | The hull in snapshots and on the wire | Snapshot vectors updated; the size test still passes at 4 ships and 24 NPCs |
| 11.4 | Launch screen: choose a hull; the stake shows its replacement price | A browser test of the stake shown |
| 11.5 | Loss: a lost hauler or gunship returns the pilot in the starter hull, with an instrumentation event | A sim test of the outcome; a ledger conservation test |
| 11.6 | Each hull drawn from its model, with its own silhouette for readability | The greybox's readability measures run with each hull |

### M12 · Sites and the site map

*At the end: you pick one of three sites on a site map, each with its own pockets, hazard and
threat.*

| # | Work item | Proof |
| --- | --- | --- |
| 12.1 | The owner models the navigation array at two tiers with its tells, through the pipeline | `check` passes and CI's art check is green |
| 12.2 | The Shattered moon and the Ice ring as site templates, with their pockets keyed to capability tags | The same seed gives the same site for each; a second seed differs |
| 12.3 | The sentinel and the stalker; debris everywhere and the ion storm | A sim test for each behaviour and hazard |
| 12.4 | The site map at preparation, with a contract per site and the navigation array's adjacency | A browser test takes a contract on the second site |
| 12.5 | Gate events and the report carry the site, so later measures can be compared by site | A report test on a two-site fixture |
| 12.6 | The caps benchmark on each site at its busiest | A recorded frame-time and snapshot-size result |

## 4. Size

The plan has 45 work items, and the Stage 1 expansion (M8 to M12) adds 33 more. Some will split
once work starts, so expect 55 to 75 pull requests for M0 to M7 and about 30 more for the
expansion.
The plan gives no dates: with agents writing the code, the pace is set by how many pull requests
the owner can review in a week. Once M0 and M1 are done, their actual pace gives a real figure
for the rest.

## 5. Risks

| Risk | Where it shows | What the plan does |
| --- | --- | --- |
| Extraction is not fun | M3 | A hard checkpoint before anything is built on the loop |
| Snapshots do not fit 1,000 bytes | M0.4, M2.6 | A size test from the first codec; projectiles travel as events, not in snapshots |
| Prediction feels wrong under latency | M1.4, M1.5 | Measured with injected delay and loss before combat is built on it |
| Firefox or Safari fail the transport | In play | Not tested, by the owner's decision; with no fallback, a failure reopens the transport decision |
| `web-transport-quinn` is pre-1.0 and changing | Throughout | Keep it behind the transport interface and pin its version |
| Windows builds | M0.1 | Proven in CI before anything depends on it |
| Facility art waits on Blender | M5.1 | It has no code dependency and can start any time after M0 |
| Passkeys on shared playtest machines | The playtest after M7 | Each tester has their own operating-system account (AGENTS.md §8) |
| The owner's modelling sets the pace of every milestone | M8.4, M9.2, M9.3, M10.1, M11.1, M12.1 | A facility marked `art: pending` can merge its rules before its model; art has no code dependency once the pipeline exists (M8.1 to M8.3) |
| Tens of hand-made models drift apart in scale, palette or names | From M8 | One contract, one material library and one tool check every asset; CI rejects an export the tool did not check |
| A wingman makes runs trivial, or feels like a burden | M8 | Threat and yield scale for it but the pot does not; the M8 checkpoint reworks its values before M9 |
| New hulls or sites break the snapshot budget | M11.3, M12.6 | Ships stay at four per instance; the size test and the caps benchmark are re-run |
| A larger station hides ships or crowds the dock | M9 | The greybox tests its silhouette before the owner models it (9.1); gates 2 and 4 are measured again (9.7) |
| The owner's hand-made exterior drifts from what the game needs | M9.2 | The pipeline's `check`, run on every export, fails on a missing mouth, a blocked lane or a missing hardpoint |
| With the modules inside, the outside stops showing the outpost's growth | M9 | Each facility and the reactor keep a tell on the exterior; gate 1's images show both views |
| The expansion swallows the gates | M8 to M12 | M7 measures Stage 1 before the expansion starts; gates 1 and 3 stay unproven until other players are measured |
| A solo measure is read as a pass | M7 | Each measure carries its basis, and a measure that needs other players is unproven, never met |
| Stage 2 is built on a loop no other player has tried | After M7 | Accepted by the owner (AGENTS.md §8); gates 1 and 3 stay unproven until other players are measured |

## 6. Open items for the owner

1. **Passkeys on machines the owner provides.** A passkey lives in the operating-system account
   of the machine it was made on. On shared playtest machines every tester's passkey would sit in
   the owner's account, behind the owner's PIN or fingerprint. Options: a hardware security key
   per tester, a separate operating-system account per tester, or a playtest-only sign-in that
   bypasses passkeys.
2. **Blender.** The agents' environment has no Blender, so 5.1 produces a script change that the
   owner runs. Regenerated binaries are committed deliberately (AGENTS.md §6).
3. **The copyright holder's name** in `LICENSE`.
4. **The touch build's stage** is still open in the design. It does not affect Stage 1.
