# Frontier Outpost: the game

How the Limit Theory engine in this repository becomes Frontier Outpost, a multiplayer sandbox
space game for small groups: open PvP under law zones, an authored frame around emergent stories,
and a player arc that runs from one ship to a faction. Part A is the game, Part B is what the
technology must become, and Part C is the order of work.

- **Status:** Proposed, 2026-09-26, for the owner's review. It governs nothing until the owner
  accepts it. AGENTS.md and `Design/ADR/` govern how code is written; this document says what is
  built (AGENTS.md, preamble).
- **Decisions so far:** the owner's G1 to G4, 2026-09-26 (§2).
- **Evidence:** the tree at `287340b`. Four read-only audits surveyed the engine, and every claim
  here about the code was then checked against the source by hand. Engine paths are relative to
  `FrontierOutpost/src/liblt/` and script paths to `GameData/script/`; `launch.cpp` is
  `FrontierOutpost/src/launch/launch.cpp`. Nothing was built or run, because the environment that
  wrote this has no MSVC. Counts come from the source. Estimates are marked as such, and none may
  enter an ADR until it is measured (AGENTS.md §6).

## 1. Summary

**Today** the repository holds a capable engine and fifteen single-scene apps. Each app is an LTSL
script that builds a world when it starts and advances it every frame by the frame's wall-clock
time (`App/war.lts:82-89`). Only `ltheory` and `dogfight` resemble a game. Nothing a renderer needs
is missing, and almost everything a game needs is: there is no game flow, no save, no network, and
no second system simulated. The economy creates money from nothing and has no consumers, and
nothing handles a destroyed player (§4).

**The game** is a persistent cluster of star systems, the Marches, with one server for each group of
2 to 16 players. AI factions trade, build and fight there with the same verbs as the players, and
carry on while nobody is online. Security falls away with distance from the Gate, from certain
punishment near it to none in the deep. A captain grows from one ship into a fleet, then outposts,
then a faction. Each server lives through an era with a beginning, a middle and an end, and writes
its own history, which the Chronicle makes readable (Part A).

**The change** is to the simulation's foundations, not a feature on top of them. The server must own
a world that runs on a fixed tick without a window, GPU or audio device, names every object by a
stable identity, saves and loads, and replicates to clients that predict only their own ship. The
engine does none of that today (§4.4). Everything else the game needs is new game code on top: a
working economy, faction AI, travel between systems, death and loot, law, and the Chronicle
(Part B).

**The order** is foundations, then a playable multiplayer core, then the living frontier, then the
tiers of the player's arc and the story (§14). Every phase ends in something playable, because the
whole is several years of work for a small team.

## 2. Decisions

The owner's, taken on 2026-09-26:

| # | Decision |
|---|---|
| G1 | **Small groups.** One persistent universe per server, for 2 to 16 players, run dedicated or hosted by a player. |
| G2 | **Open PvP with law zones.** Security set by system, consequences for aggression, and loot on death, in the manner of EVE. |
| G3 | **Emergent story in an authored frame.** A written setting and a server-wide arc of milestones, with the simulation writing the rest. |
| G4 | **Pilot, then fleet, then outposts, then faction.** |

What this document proposes, for the owner to accept or reject. Each engineering proposal becomes an
ADR in the commit that implements it (§15).

| # | Proposal | Where |
|---|---|---|
| P1 | The server is authoritative and replicates state. There is no lockstep and no peer-to-peer authority. | §13.1 |
| P2 | One executable in two roles. Single-player and hosting both run the server role as a local child process. | §13.2 |
| P3 | The star system is the unit of simulation fidelity, network interest, persistence and law. | §12 |
| P4 | A fixed simulation tick, owned by the engine. The tick is the clock. | §13.4 |
| P5 | Simulation and presentation are separate. The server builds nothing on the GPU, plays nothing, and depends on no camera. | §13.4 |
| P6 | Seeds for looks, the server for facts. | §12 |
| P7 | Clients hold a replica world made of the engine's own objects, so the LTSL interface keeps working. Every change a client makes to the world is a command. | §13.6 |
| P8 | The world is generated once per era and then owned by its save. There is no seed-and-delta scheme. | §13.7 |
| P9 | LTSL stays for the interface, tools and content generation, and runs in both roles. The rules of play are C++. | §13.8 |
| P10 | Transport is a new game-agnostic static library, NeuronNet, over Winsock, linked into `lt.dll` as NeuronClient is. No third-party networking library. | §13.6 |
| P11 | The simulation replays on the same binary and machine: seeded random streams, iteration ordered by identity, no wall clock. | §13.4 |
| P12 | Two fidelities. *Live* systems, which have players in them, are simulated in full. *Ledger* systems run as accounts and schedules. | §13.5 |

## 3. Where the brief is weakest

1. **"Convert this to a complete game" understates the distance.** What exists is a renderer, a
   flight model, procedural generation, an interface toolkit, and fragments of an economy and an AI
   (§4.3). Beyond `dogfight`'s shooting, no loop closes. A player cannot sell, dock, refit, repair,
   research, hire or leave the system, and nothing happens when their ship dies
   (`Component/Pilotable.cpp:8-9`: "TODO : What if player dies?"). A complete game here is mostly
   new game code, on a simulation that must first be rebuilt for multiplayer.
2. **Multiplayer changes the foundations.** The simulation has no tick of its own, takes gameplay
   facts from the GPU and the camera, names objects by a process counter with no registry behind
   it, cannot save, and updates one system (§4.4). All of that must change before the first packet
   is worth sending, which is why M0 has no networking in it (§14).
3. **Open PvP at 2 to 16 players (G2) is the weakest of the owner's choices.** EVE's model works
   because thousands of players supply victims, predators, police, bounty hunters, and a market
   that replaces losses. On a server of sixteen, one aggressive player who is online longer than
   everyone else can make it unplayable, and nobody is there to counter them. The design keeps G2
   and pays for it with six mechanisms (§9.2): AI police and bounty hunters, unconditional safety
   in Chartered space, protection for offline assets, rewards that follow risk rather than the
   victim's weakness, server rule sets, and dedicated servers for PvP among strangers. If that is
   not enough in play, the fallback is the Lawful rule set, not a redesign.
4. **A persistent world for a small group is mostly an AI world, and the AI does not work yet.** The
   one brain, `Task_Play`, adopts a random opportunity each update (`Game/Task/Play.cpp:167-174`).
   Three of the fifteen tasks can never look profitable, because they declare `GetOutput` where the
   base class has `GetOutputs` (`Game/Task/Pirate.cpp:62`, `Produce.cpp:56`, `Spawn.cpp:28`;
   `Game/Task.h:56`). AI players act only while piloting a ship, so governors and station managers
   never act (`Component/Pilotable.cpp:5-6`). Pillar 1 (§5) rests on the largest body of new game
   code in this plan.
5. **"Hosted by a player" cannot mean in the same process.** The engine keeps one world per process:
   `Universe_Get()` returns the top of a global stack (`Game/Universe.cpp:15-19`), and the ID
   counters, pools and settings are process-wide. Hosting therefore starts the server role as a
   child process, which also makes single-player the same code path as multiplayer (P2). The cost
   is a second process on the host's machine; the gain is that no mode is special.
6. **The authored arc (G3) must survive servers whose players keep different hours.** An arc paced by
   the clock leaves quiet servers behind, and one paced by progress stalls on them. Milestones
   therefore trigger on world state, within an earliest and a latest time, and every new captain
   gets a personal opening whichever act the server is in (§10.2, §10.6).
7. **G4 is five games' worth of systems.** Pilot, captain, commander, founder and faction leader each
   need their own verbs, rules and interface, and only the first is close to existing. The phases
   ship them in that order, and each must be playable alone before the next begins (§14).
8. **Procedural generation is the engine's strength and a multiplayer hazard.** Gameplay facts come
   from generated render meshes, and some of them are random per run. Sockets are ray-cast against
   the generated hull (`Game/Item/ShipType.cpp:223-259`). Bounds come from the renderable
   (`Game/Object.cpp:165-166`, marked "CRITICAL"). Asteroid shapes, and their collision with them,
   change between runs, because they are seeded by `Rand` after `srand(time(0))`
   (`Game/Renderable/Asteroid.cpp:17`, `LTE/Program.cpp:17`). A server and its clients would
   disagree about the world; P5 and P6 exist for this.

## 4. Starting point

### 4.1 The engine

`lt.dll` is the 2012–2015 Limit Theory engine (LTE), its game layer and its scripting language
(LTSL): about 66,000 lines of C++, imported under ADR-001's legacy exemption and moved onto
NeuronClient and Direct3D 12 by the migration recorded in `Design/Archive/`. `launch.exe` runs one
LTSL app, named on its command line, in a 1920×1080 window (`launch.cpp:62-66`). `GameData/` holds
121 scripts (9,274 lines), four font families, 43 WAV sounds, eight texture files and one data
file, the naming grammar.

### 4.2 A script per scene

An app is a script that builds a world. `war.lts` creates a camera, two interfaces and a system,
instantiates 32 ships from four generated types, and sets each after the first to destroy one of
those before it. Then, every frame, it reads `FrameTimer_Get`, clamps it to 0.1 s, multiplies it by
20 while H is held, and calls `system.Update dt` before drawing (`App/war.lts:82-93`).
`ltheory.lts` builds a universe from seed 39, puts a ship near the first station, and updates only
the container the player's ship is in (`App/ltheory.lts:80-83`).

There are two conventions: an `App` type with `Initialize` and `Update` (war, dogfight, rails, map,
market), and a widget tree hosted by `App/widget.lts` with up to 14 named hooks (the other ten;
`UI/Widget/Custom.cpp:111-131`). Neither has a game flow. No app can start another, because the
DevPanel's app list is only text. The game menu's SAVE GAME and EXIT buttons send a message that
nothing handles (`Widget/GameMenu.lts:4-8`), and an app ends when its window closes. The scene
model survives as test scenarios (§13.8).

### 4.3 What works, what is a fragment, what is missing

| Area | State at `287340b` | Evidence |
|---|---|---|
| Flight | **Works.** Newtonian motion with drag. The AI steers by potential fields; the player flies by keys and mouse. The player's heading is set directly by a script, not by thrust. | `Component/Motion.cpp:10-45`, `Component/MotionControl.cpp:21-60`, `Widget/Handling.lts:35-69` |
| Combat | **Fragment.** Pulse turrets hit through shields to the hull, and every generated weapon is a pulse weapon; beams are `#if 0`. Shields recharge from a power fraction that ships do not supply (by reading the code). Collision damage is off, and collisions are checked only for objects a human owns. Destroyed ships are not removed, and scanners return before detecting anything. | `Game/Item/WeaponType.cpp:65-67,101-112`, `Game/Object/Shield.cpp:171`, `Game/Object.h:399-401`, `Component/Collidable.cpp:41-43,62-65`, `Component/Explodable.cpp:7-16`, `Game/Object/Scanner.cpp:31-32` |
| Travel | **Fragment.** Warp rails inside a system work, for the AI and for the player (Y and U). Wormholes join systems, but route planning ignores them ("CRITICAL"), no player input reaches one, and only one system updates. Docking works for the AI only, and `CanDock` is inverted. | `Game/Task/Goto.cpp:100-102,240-243`, `App/ltheory.lts:66-78`, `Component/Dockable.cpp:15-16` |
| Economy | **Fragment.** Order-book markets with escrow, matched at the mid price. Scripts create the money: 1.5 M per AI player, and 1 M per governor and per station manager. Each planet governor gets 10⁹. A partly filled bid is refunded its whole escrow, which creates credits. Transport moves nothing and nothing consumes. Production and research cannot be reached, because no blueprint is ever created. | `Component/Market.cpp:102-118`, `Object/System.lts:1,61,156,212`, `Game/Object/Planet.cpp:105-106`, `Game/Task/Transport.cpp:44-49` |
| AI | **Fragment.** Each object has a task stack, and there are 15 tasks. The one brain, `Task_Play`, adopts a random opportunity from a list that each system rebuilds every update, at a cost of O(markets² × items). | `Component/Tasks.cpp:13-22`, `Game/Task/Play.cpp:167-174`, `Component/Economy.cpp:99-139` |
| Generation | **Works.** One universe holds one region, and the region holds 1 + ⌊5·Exp⌋ systems. `Object/System:Init`, in LTSL, fills each system with a planet and its colonies, radial rails, two asteroid zones of 96 rich asteroids, stations with 32 listings, patrols, pirates, and two AI players. That is about 380 to 400 objects per system (an estimate from the script, not a count at run time). | `Game/Universe.cpp:80`, `Game/Object/Region.cpp:20,95-96,153`, `Object/System.lts` |
| Story hooks | **Skeletons.** Names come from a grammar, and there are seven personality traits. Opinions are keyed by `ObjectID` and never fade. The history component never runs. Each object keeps a log. Seven lifetime stats exist and are never written. Missions exist, but their constraints all evaluate to 1.0. | `LTE/Grammar.cpp`, `AI/Traits.h`, `Component/Opinion.h:8`, `Component/History.cpp:5`, `Component/Log.h`, `Game/Player.h:47`, `Game/Mission.cpp:14,29` |
| Interface | **Mostly works.** The HUD, a system map, an object inspector (5 of its 12 tabs have content), and a market with a working order dialog. The game menu's buttons do nothing. The settings window calls `WidgetSettings`, a native that ADR-013 removed, and its only caller is commented out. | `Widget/HUD.lts`, `Widget/Map.lts`, `Widget/ObjectInfo.lts:135-164`, `Widget/Market/Transaction.lts:162-181`, `Widget/Settings.lts:13`, `App/widget.lts:28-41` |
| Time | **Missing.** Frame time drives the world. `kDefaultSimulationFrequency` is declared and never used. The universe's age never advances, so every log and trade timestamp is 0. | `Game/Object.cpp:27`, `Module/FrameTimer.cpp:26`, `Game/Universe.h:26` |
| Persistence | **Missing, with a foundation.** No game state is saved. A versioned binary serializer, driven by reflection, exists and saves the settings and the disk caches. | `LTE/Serializer.h:15-42`, `Module/Settings.cpp:39-45` |
| Network | **Missing.** What little existed was removed as unreachable. | ADR-013, ADR-005 |
| Front end, input | **Missing.** One app per process. Keys are literals in scripts, with no rebinding. The settings file has no setter. | `launch.cpp:209`, ADR-013, `Module/Settings.cpp` |
| Mods | **Minimal.** Whole-file overrides from `mod/*/`; the last one read wins. | `LTE/Location.cpp:17-41` |

### 4.4 What blocks multiplayer, most work first

1. **The simulation depends on the GPU, the renderer and the camera.** Building a system creates GPU
   textures and a 100,000-star mesh (`Game/Object/System.cpp:81-114`). A ship type runs its hull
   generator, which bakes occlusion on the GPU (`Item/ShipType/Generate.lts:89`), and then
   ray-casts its sockets against that hull (`Game/Item/ShipType.cpp:219-259`). Bounds and
   collision meshes come from renderables (`Game/Object.cpp:165-166`), and asteroid fields exist
   only on the GPU (ADR-009). Zones create and delete asteroids while *drawing*, around the camera
   (`Game/Object/Zone.cpp:62-100,119-124`), and simulation code plays sounds and spawns lights and
   explosions. A server has no GPU, no audio and no camera.
2. **There is no tick.** The frame's wall-clock time is the step (`Module/FrameTimer.cpp:26`). The
   player's control script sets the ship's heading from the frame time and moves the camera in the
   same function (`Widget/Handling.lts:35-69`), and some behaviour counts updates rather than time.
3. **Identity.** IDs come from one process-wide counter that presentation objects consume too
   (`Game/Object.cpp:32-37`). The counter is never saved (`LTE/Static.h:6`), and nothing maps an ID
   back to its object. `parent` and `container` are raw pointers into pooled memory that is reused
   last-in-first-out, and deleted objects linger while anything holds them.
4. **Persistence coverage.** The serializer walks reflected fields, but renderables ("CRITICAL :
   Serialization of renderable", `Component/Drawable.h:10`), the universe's own members and the
   pilot link are not reflected, and an unreflected type is copied as raw bytes
   (`LTE/Type.h:432-440`). Reflected type storage also differs between `launch.exe` and `lt.dll`
   (`launch.cpp:102-107`), so code that walks the reflection must live in `lt.dll`.
5. **Nondeterminism.** `srand(time(0))` at start-up (`LTE/Program.cpp:17`) feeds weapon cooldowns,
   docking, mining, AI choices and asteroid shapes. Cargo, databases and storage are `std::map`s
   ordered by memory address (`Component/Cargo.h:10`, `LTE/Reference.h:110-112`). Nothing replays.
6. **One system is simulated.** Nothing updates the universe, the region, or any system the player
   is not in.
7. **Update cost grows with the world**: the opportunity rebuild above, a spatial hash rebuilt every
   update, and every pulse ray-marching that hash.
8. **Reference counts are not atomic** (`LTE/Reference.h`), so no network thread may touch an
   object.

## Part A: The game

## 5. Pillars

Five pillars decide trade-offs. When two features compete for the same time, the one that serves
more pillars wins, and a feature that serves none is cut.

1. **The frontier lives without you.** AI factions trade, build, fight and expand with the players'
   verbs, and the server keeps simulating them while nobody is online. On a server of 2 to 16 most
   of the population is AI, so the AI *is* the content. Every player verb must therefore also be an
   AI verb, and the simulation must run at more than one level of detail (§13.5).
2. **One ship, then a fleet, then a flag.** A captain's agency grows by adding verbs, not numbers:
   fly, own several ships, command them, found outposts, lead a faction.
3. **Law is geography.** Security falls away with distance from the Gate. Where you are decides what
   can happen to you, what you can earn, and who comes when someone breaks the law. PvP is possible
   everywhere outside Chartered space, and its consequences are known before you act.
4. **Every server writes its own history.** An authored frame gives each server a beginning, a
   middle and an end. The simulation and the players write the rest, and the Chronicle makes it
   readable.
5. **Built for a small crew.** On a persistent server of friends, being offline is the normal state.
   Two players online on a Tuesday must have as good an evening as ten on a Saturday, and one bad
   actor must not be able to ruin a server.

## 6. Setting: the authored frame

Every name here is a placeholder for the owner to replace (§19, Q1). What matters is the job each
element does.

The **Marches** are a cluster of star systems, reached from settled space, the **Core**, through a
single unstable wormhole, the **Gate**. The Gate opens in **Tides**, and between Tides nothing
crosses in either direction. The Core's **Charter Authority** licenses captains to explore, claim
and settle the Marches, and promises that claims still held when the Gate next opens will be
recognised. Earlier Tides brought the AI factions that are already established when the players
come through.

That small frame does six jobs. It bounds the cluster to tens of systems, not a galaxy, which both
G1 and the engine's budgets need (§13.5). It gives the law gradient a reason: the Authority's writ
is strongest at the Gate and fades with distance (§9.2). It closes the economy, because between
Tides nothing comes from the Core, so there is no bottomless vendor (§9.3). It makes the players
newcomers among incumbents who already hold territory, trade routes and grudges, so the first hour
has someone to work for, someone to sell to, and someone to be afraid of. It gives each server a
clock, the next Tide, and with it a story that has a shape (§10.2). And it leaves a mystery for
later: the wormholes and warp rails are older than anyone who uses them, and whoever built them is
gone. Later arcs can open that door; the first release does not need to.

**Faction archetypes.** Factions are generated, and their names already come from the grammar's
`$faction` rule, whose suffixes are brotherhood, cartel, collective, corporation, imperium,
industries, guild and technologies (`GameData/gamedata/grammar/default.txt`). An archetype says
what a faction wants, and each is a preset over the seven traits in `AI/Traits.h`:

| Archetype | Wants | Law | What it is to a player |
|---|---|---|---|
| Charter Authority | order near the Gate, taxes, stable trade | enforces it in Chartered space | issues charters, licences, bounties and insurance; runs the mint |
| Corporation, industries | production chains, trade routes, resources | lawful; hires security | employer, customer, competitor |
| Guild, collective | research, crafts, independence | neutral | research contracts, rare blueprints |
| Cartel, brotherhood | smuggling, piracy, protection money | lawless | a predator, or an employer of criminal captains |
| Nomads | the rails, salvage, freedom | lawless, not hostile | relic traders, guides to deep space |
| Imperium, zealots | to rule the Marches, or to seal the Gate | their own | the arc's antagonists, or its allies |

## 7. The player's arc

| Tier | You are | Verbs this tier adds | What the engine already has | New interface |
|---|---|---|---|---|
| T0 Pilot | the captain of one chartered ship | fly, dock, trade, mine, fight, survey, take contracts | flight, pulse weapons, markets, the HUD | contract board, onboarding |
| T1 Captain | the owner of several ships | buy, salvage or win ships; switch between them; hire wingmen | `Player.Pilot`, `AddAsset` | hangar, wing commands |
| T2 Commander | the owner of a working fleet | standing orders: trade route, mine and sell, patrol, escort, hunt | the task stack and its 15 tasks | fleet panel, orders on the map |
| T3 Founder | the owner of outposts | deploy a station kit, fit modules, produce, research, set prices, post contracts | stations, production and tech labs, sockets, missions (skeletal) | construction, production, market administration |
| T4 Faction leader | the head of a faction of players and NPCs | claim systems, set their law and tariffs, conduct diplomacy, declare war | opinions, AI players | faction, territory, diplomacy |

Progression comes from capital, standing and assets, never from experience levels. The tiers are
soft gates: an outpost needs a station kit, which costs money and, in Chartered space, a licence.
Nothing is locked behind a level, and a player can stay a pilot for good and be a good one.

## 8. Core loops

Four time scales feed each other. In **seconds**, the loop is flight, combat, mining and docking,
which `dogfight`, `war` and `handling` already show in spirit. A **session** of 30 to 90 minutes is
choosing work (a contract, a trade run, a survey, a hunt), travelling and doing it, then coming
back to spend the proceeds on repairs, a refit, a new ship or an outpost module. Over **weeks**,
capital becomes assets, assets become standing with factions, and standing becomes territory. Over
the **era**, weeks to months, the arc's acts, the factions' fortunes and the Chronicle unfold.

A session must be worth having at every tier. A founder with twenty minutes can still fly one
contract, and a pilot's twenty minutes still move a faction's numbers a little.

## 9. Systems

### 9.1 Flight and combat

Flight keeps the engine's model: Newtonian motion with drag, 0.8 linear and 2 angular
(`Component/Motion.h:10-11`), thrusters as fitted equipment, and warp rails as the highways inside
a system, with nodes every 15,000 units (`Object/WarpNode.lts:2`). The player's heading stops being
set directly by an interface script and becomes part of the flight command the server applies
(§13.4), as boost and cruise do; their messages already exist (`Game/Messages.h`).

Between systems, the wormholes are the jump points. Each region already joins its systems along two
randomised spanning trees (`Game/Object/Region.cpp:163-188`). A jump is a handover between
systems whose short transit hides the load (§13.5), and the AI's route planning over the cluster's
graph fills the empty branch in `Game/Task/Goto.cpp:240-243`.

Weapons get their four classes back. Pulse is the only one that fires today. Beam is hitscan and
lag-compensated (§13.6), and is `#if 0` today. Missile is guided, and rail is a fast projectile.
The generator's class weights, `{0, 0, 1, 0}` today, move into tuning data
(`Game/Item/WeaponType.cpp:65-67`). Power becomes the fitting constraint: generators supply it, and
shields, weapons and engines draw on it. Shields already recharge from it
(`Game/Object/Shield.cpp:171`); ships just never supply any. Scanners, disabled today
(`Game/Object/Scanner.cpp:31-32`), read the signatures the engine already defines and decide what
can be targeted. That makes sensor fits and stealth part of combat, and it also decides what the
server sends each client (§13.6). Collision is checked for every mover and deals damage within
limits. Today it is checked only for objects a human owns, and it never deals damage
(`Component/Collidable.cpp:41-43,62-65`).

Ship roles follow scale, and the engine already names the classes, from Fighter through Corvette,
Destroyer and Battleship to Colossus (`ScaleToClass`, `Game/NLP.cpp`). A hull is generated from one
budget, its value, which is split between hull, thrusters and generator
(`Game/Item/ShipType.cpp:188-196`). So roles are trade-offs within a budget, and no hull is best at
everything.

**Death is loss, not failure.** The ship is destroyed, and the captain ejects and respawns at their
registered berth, a station where they have docking rights. Each cargo stack and each fitted module
drops with a set chance (proposed: 50%, tuning data) as a cargo pod, and the rest is destroyed. The
hull is lost, and Authority insurance refunds part of it unless the owner committed a crime. The
pod already exists: `Pod` holds an item and a quantity, and anything with cargo that touches it
takes it (`Game/Object/Pod.cpp:59-76`). Nothing spawns one in space today. Loot is free to take in
Lawless space; elsewhere, taking another captain's loot is theft.

### 9.2 Law, crime and PvP

Security is set per system when the era is created, from the number of jumps to the Gate, and play
changes it afterwards: the Authority thins out in Act II (§10.2), and factions claim systems (§9.8).

| Level | Who enforces | Attacking a captain who has not consented | Structures | Pay-off |
|---|---|---|---|---|
| Chartered | the Authority, with certainty | A crime. An Authority response arrives within seconds and cannot be out-fought, and the attacker gets a bounty, a standing loss and void insurance. | immune to players | lowest; safe hubs and starter work |
| Held | the holder's patrols, if they arrive in time | A crime against the holder. The attacker is flagged outlaw, so anyone may attack them freely while the flag lasts, and gets a bounty and a standing loss with the holder. | attackable in the owner's vulnerability window | middle |
| Lawless | nobody | Not a crime; only opinions and reputation move. | attackable after reinforcement timers | the best ores, relics, anomalies |

Crime is an event, judged by the law of the system it happens in. Damage is already an event, and
destruction is declared as one but never defined (`Game/Events.h`, Appendix A7). The HUD states
what an act will cost before it happens: "Attacking this ship is a crime here: Authority response,
bounty 12,000." A duel needs mutual
consent and is legal anywhere. Faction war is declared with a cost and a delay, appears in the
Chronicle, and makes the two factions' members legal targets for each other outside Chartered space.
Chartered space is always safe. Bounties are posted by victims, factions and the Authority, and are
paid from escrow. Criminals are refused docking at lawful stations and need the cartels' havens,
which gives lawless play its own geography.

**Six mechanisms make open PvP work at 2 to 16 players** (§3, point 3):

1. **The AI is the police, the bounty hunter and the pirate.** A bounty is hunted by AI hunters whose
   strength scales with its size, so a crime costs something even when no human wants to chase the
   criminal.
2. **Chartered space is unconditionally safe**, so a new or weaker player always has somewhere to
   earn.
3. **Offline assets are protected.** A captain who logs off in space leaves after a timer (proposed:
   30 s, or 2 min after combat), so logging off does not escape a fight. Docked ships are safe.
   Outposts outside Chartered space can be attacked only in a daily window their owner chooses, and
   only after their shields have been reinforced once.
4. **Rewards follow risk, not the victim's weakness.** Destroying a much weaker ship pays nothing, and
   the standing it costs grows with the gap in strength.
5. **Server rule sets**, a setting rather than a mode: *Frontier*, the default, as above; *Lawful*,
   PvP only by consent or declared war; *Anarchy*, where Chartered space is the Gate system alone.
6. **Dedicated servers for PvP among strangers.** A listen server's host has no latency and holds the
   whole world in memory. Among friends the host is trusted; among strangers they should not need to
   be.

### 9.3 Economy

Five principles govern it. **Goods are physical**: they sit in holds, move by ship, and can be
stolen, lost or blockaded, and nothing teleports between markets. **Money moves between accounts,
except at named faucets and sinks**, each a tuning value whose flow the server reports (§13.11).
**Prices come from stock and demand** at each market. **The AI factions are most of the producers
and consumers**, and a player can take any role in the chain. **Value is energy**:
`Game/Constants.h` defines value in resource units, with conversions to mass, capacity, integrity
and output. They are unused today, and become the balancing basis.

Goods come in four tiers, plus data. Raw goods are the universe's eight generated ores and its basis
ore (`Game/Universe.cpp:49-63`), and the ice and gas that zones take as arguments and never use
(`Game/Objects.h:154-156`). There is one refined good per raw good. Manufactured goods are
components, equipment, hulls and station kits, and consumables are what colonies live on. Data
(survey data, charts and blueprints, carried in the database hold) is cargo that can be sold,
copied, stolen or lost (§9.4, §9.7). Production converts goods at rates stated in resource units,
in refineries, factories and shipyards (§9.6), with the item's assembly chip as the recipe. The
chip's requirements are `#if 0` today (`Game/Item/AssemblyChip.cpp:21-31`).

Consumers are what the economy lacks. Colonies consume goods, pay for them from population income,
and grow or shrink with supply; the colony already has a population field that nothing uses
(`Game/Object/Colony.cpp:156`). Outposts consume upkeep, shipyards consume components, and missiles
consume ammunition.

**Money is Charter scrip.** The engine already backs value with an ore, the universe's
`currencyBasis` (`Game/Universe.cpp:53`). The design makes that literal: the Authority's mint
converts delivered basis ore into scrip at a set rate. The two faucets are the mint and colony
income. The Authority pays bounties and contracts from its taxes, so its spending mostly recycles
money. The sinks are destruction, fees and taxes, wages, upkeep, insurance premiums (a net sink
while losses are low) and research. The money supply is watched against a target band, with the
mint rate as the one lever, which the arc may also move: in Act II the Authority mints less. The
pay-off is that control of the basis ore becomes a strategic objective for factions. The risk is
deflation if the sinks outrun the mint, which is why the flows are instrumented from the first day
(§13.11) and balanced by soak tests (M4).

Markets keep the engine's order book, escrow and mid-price matching (`Component/Market.cpp`), with
its refund fixed (Appendix A1). Each station's owner runs a market maker that quotes around a
reference price of value × scarcity, where scarcity is stock against the owner's target. What a
captain knows of prices elsewhere is what they last saw or bought (§9.4), so hauling pays for
information as well as for distance. Contracts grow from the engine's `Mission`, which has an owner,
a pool, a price and constraints on the item (`Game/Mission.h`). Every contract's reward sits in
escrow. Its kinds are procurement (the engine's case), delivery, courier with collateral, bounty,
survey and escort. AI factions post most contracts, and players post their own from T3.

### 9.4 Exploration and information

The cluster starts mostly unknown: the wormhole graph shows that a system exists, not what is in it.
Surveying scans a system's objects into survey data, which the Authority's and the Nomads'
cartographers buy. Knowledge of markets, stations, ship types and anomalies belongs to a captain and
their faction, and it goes stale. The engine's `Info` component already models knowledge that
expires, and never expires it today because the universe's age never advances
(`Game/Universe.h:26`). Stale prices are a feature: they are what a trader bets on. First
discoveries go into the Chronicle and count towards the Discovered stat. Naming rights for the first
surveyor are a server setting, because names need moderation. Anomalies are the Director's (§10.3):
derelicts with salvage and a fragment of story, relic rails, rare ore fields, and transient
wormholes into hidden systems.

### 9.5 Ships, crews and orders

A captain owns ships bought at shipyards, salvaged from wrecks or won in contracts, and can pilot any
owned ship docked where they are; `Player.Pilot` already switches the piloted ship
(`App/ltheory.lts`). NPC captains are hired at stations, each with a generated name, the seven
traits, a wage, and loyalty, which is their opinion of their employer. Traits steer behaviour. An
*Aggressive* captain engages sooner, a *Greedy* one asks more and may skim, and a *Lawless* one
accepts criminal orders. *Explorative*, *Intellectual* and *Creative* captains survey, research and
produce better, and a *Sociable* one keeps a wing together. The engine's like and dislike
thresholds (±0.5, `Game/Object.cpp:28-29`) decide when a captain quits or defects.

Orders use the AI's own tasks, so the design stays symmetric: Goto (with travel between systems),
Dock, Mine, Transport (which must actually carry goods, `Game/Task/Transport.cpp:44-49`), Buy, Sell,
Destroy and Hunt. They are joined by new tasks for Escort, Survey and Salvage, and by Patrol, which
ADR-013 deleted as unreachable and which comes back. A standing order is a loop of tasks with
conditions (`Condition` exists, with one case, Nearby). The engine's `Project` already assigns
assets to a goal with a wallet, which is what a trade route needs. A wing follows its captain and
takes four commands: attack my target, hold, return, dock. While its owner is offline, a fleet works
its standing orders at ledger fidelity (§13.5). What happened lands in the owner's log and digest
(§10.4), and losses are real.

### 9.6 Outposts

A station kit, built at a shipyard, unpacks into a station of a generated `StationType` at a legal
site. That means far enough from other stations, with a licence in Chartered space, and with the
holder's leave in Held space, where anything else is a hostile act. Modules plug into the station's
sockets through the engine's `Sockets` and `Pluggable` components:

| Module | Does | Engine base |
|---|---|---|
| Trading post | a market the owner prices | `Market` |
| Storage | lockers per captain | `Storage` |
| Refinery, factory | production queues | `ProductionLab` |
| Laboratory | research | `TechLab` |
| Shipyard | hulls and station kits | new |
| Habitat | a colony seed: crew, consumption, growth | `Colony` |
| Power | the power budget every module draws on | `PowerGenerator` |
| Defence | turrets, shield, drone bay | `Turret`, `Shield`, `DroneBay` |
| Berth | a respawn point | new |
| Law beacon | claims the system (T4) | new |

Upkeep is paid in scrip and supplies, and staff are NPC crews. Outside Chartered space, an outpost
whose shields fall enters a reinforced state (proposed: up to 24 hours), and can then be destroyed
only in its owner's daily window. A destroyed outpost leaves a wreck to salvage and a Chronicle
entry. AI factions found outposts the same way, so their expansion is something players can see,
contest and profit from.

### 9.7 Research and technology

An item type is generated from a value, a seed and a few multipliers (`Game/Items.h`), so technology
is value and quality, not a tree. A blueprint is the right to produce an item type, and research at
a laboratory derives a new one. The engine's derivation raises one parameter and lowers another by
the same factor (`Game/Item/Blueprint.cpp:48-70`); that becomes the player's choice rather than a
roll, such as more range for less rate. Each tier of value follows `Constant_ResearchIncrement`,
x + 0.25·√x, so progress is quick early and slows later. Blueprints are data: they can be copied for
a cost, traded and carried, and a copy in the hold is lost with the ship, while originals stay in
the laboratory. Relics unlock rare blueprints, which is where exploration and the story pay into
technology. Derivation draws from the laboratory's own seeded stream, not from `rand()` (§13.4).

### 9.8 Factions and diplomacy

An AI faction has an archetype and traits, a treasury, assets, held systems and goals. Its brain
replaces `Task_Play`'s random pick with projects chosen by expected utility over the ledger economy
(§13.5): expand (found an outpost), develop (production), trade (routes), secure (patrols and law),
raid (piracy) and war (claims). `Task_Manage` already splits a budget across Develop, Expand,
Monopolize and Secure (`Game/Task/Manage.cpp:139-142`); the idea stays and its stubs are filled.

Standing is an opinion from −1 to 1 that a faction holds about each captain and each other faction.
It decays towards a default, which fixes "opinions are currently permanent"
(`Component/Opinion.h:8`), and it gates docking, market access, contract tiers, and whether patrols
attack.

A player faction (T4) is chartered at the Authority for a fee. It has members, players or NPC
captains, with ranks and permissions, a treasury and shared hangars. It claims systems with law
beacons, which makes them Held space under its own law and tariffs, and it conducts diplomacy with AI
factions: trade, non-aggression, alliance, and war declared with a delay and a cost.

## 10. Storytelling

### 10.1 Three layers

The setting is written once and does not change (§6). The era arc is an authored structure that each
server plays through in its own way (§10.2). Emergent stories are generated by the Chronicle, the
Director and persistent characters (§10.3 to §10.5). The players' own stories run through all three,
and the Chronicle records them with the same weight as the AI's.

### 10.2 The era arc

An era is one pass of the arc on one server, in three acts and an epilogue.

- **Act I, Landfall.** The Tide is open, and players and a new wave of AI come through. The Authority
  is strong, and Chartered space reaches two jumps from the Gate. Surveys and the first outposts
  advance the act.
- **Act II, The Long Night.** The Gate closes. Nothing more comes from the Core, and the Authority
  lives on its taxes. Its patrols thin, and Chartered space shrinks to the Gate system, announced
  before it happens. Factions scramble for what the Authority lets go, and relics along the rails
  point to a hidden region, which the Director opens.
- **Act III, Tidefall.** Signs say the Gate will reopen, and that the Core will recognise the claims
  held when it does. A race for territory begins, and the arc's antagonist moves: zealots who would
  seal the Gate, or an imperium that would declare the Marches independent. At the Tide, world state
  decides the outcome: who holds what, and whether the Gate still opens.
- **Epilogue.** The Chronicle writes the era's summary. The server then continues as an endless
  sandbox, or starts a new era on a new seed, in which captains keep their names, titles and one
  heirloom, and the old Chronicle stays readable.

Milestones are data, not code. Each is a predicate over world metrics (the share of systems
surveyed, the number of outposts, a faction's share of territory), an earliest and a latest time,
and a list of effects: a Chronicle entry and a broadcast, changes to faction behaviour, security
shifts, a region unlocked, the state of the Gate. The earliest time stops an eager server from
racing through the story, and the latest keeps a quiet one moving. Era length is a server setting,
and the arc can be switched off. The authoring cost is a handful of acts, about twenty milestones and
their text, not a campaign.

### 10.3 The Director

The Director is a server rule that watches **tension**, for each group of online players and each
live system, from recent combat, wealth at risk, the system's security and the time since the last
event. It spends an event budget on templates, each with procedural parameters and a resolution:

| Template | What happens | Resolves by |
|---|---|---|
| Distress call | a ship asks for help; sometimes it is bait | rescue, salvage, or ambush |
| Raid | pirates hit an outpost or a convoy | a defence contract |
| Derelict | an old ship with salvage and a log fragment | salvage; the fragment feeds the arc |
| Anomaly | a transient wormhole to a hidden system | exploration, before it closes |
| Market shock | a shortage or a glut | trade |
| Expansion | a faction founds an outpost in contested space | trade, contest, or ignore |
| Feud | two NPC captains' grudge becomes a skirmish | pick a side, or profit |
| Hunter | an AI hunter comes for a criminal's bounty | fight, flee, or pay |

It respects the law, so there are no raids on Chartered stations, and it respects the act of the arc
and the number of players online. Its events are ordinary simulation events, and every one of them
reaches the Chronicle.

### 10.4 The Chronicle

The Chronicle is the server's append-only record of notable events. Each entry carries its tick, its
system, its kind, its participants (entity identities with roles), the values at stake, and an
importance. Importance grows with the value at stake, the number of participants, being the first of
its kind, player involvement and relevance to the arc, with weights in tuning data. Prose is rendered
at read time from templates and the grammar, so better templates improve old entries too, while each
entry keeps its participants' names as they were at the time.

Its views are the **Frontier Wire**, the news ranked by importance and recency; system histories on
the map; faction annals; the captain's record, with titles (§10.5); **While you were away**, the
entries about your assets since you last logged in; and **The story so far**, the era's top entries,
for a newcomer. Entries above a threshold are kept for good, and those below it are folded into
periodic summaries such as "In week 3, Winterskytech lost 14 freighters to raiders in Kess."

The engine offers the start of it: events for damage and mining; `Event_Destroyed`, declared and
never defined (`Game/Events.h:15`); a history component whose `Run` does nothing
(`Component/History.cpp:5`); and logs that already carry an importance (`Component/Log.h`).

### 10.5 Characters

Faction leaders, station managers, governors, and any captain who survives an encounter with a player
persist. Each has a name, a faction, the seven traits, decaying opinions of captains and factions, a
memory (a short list of the Chronicle entries they took part in), and goals. The Director prefers
known characters for events that involve a player: the pirate you let flee comes back with a better
ship and a grudge, and the trader you rescued sends you work. That is how the game grows a nemesis
cheaply.

Players earn titles from the seven lifetime stats that the engine already names and never writes
(`STATS_X`, `Game/Common.h`):

| Stat | Title |
|---|---|
| Accumulated | Magnate |
| Befriended | Envoy |
| Controlled | Governor |
| Created | Founder |
| Destroyed | Warlord |
| Discovered | Pathfinder |
| Invented | Artificer |

### 10.6 The first hour

1. **Arrival.** You come through the Gate on the Tide or, between Tides, on an Authority courier as a
   late charter.
2. **The charter.** An Authority officer, a generated character who will recur, hands over the charter
   and the starter ship.
3. **The first contract.** A short haul or survey in Chartered space teaches flight, the rails,
   docking and selling.
4. **A path.** You choose hauler, prospector, surveyor or security: a short contract chain that ends
   with its module, whether cargo expansion, transfer unit, scanner or weapon.
5. **The briefing.** The server's story so far, then the Frontier Wire and the contract board.

Friends can form a wing at any step, and the contracts scale to them. The target is that after sixty
minutes a new captain has flown, docked, traded and earned, has lost nothing that matters, and knows
one character by name.

## 11. The multiplayer experience

A player has one captain per server. It is identified by a key pair that the client generates, with
no account service (§13.6), and a server's owner may keep a whitelist. Players join by direct
address, LAN discovery or favourites, and a late joiner gets the first hour and the story so far.
Wings share contracts and split loot, factions share assets (§9.8), chat has local, wing, faction and
global channels, and the map takes pings.

A captain who logs off in space leaves after the logoff timer (§9.2) and reappears there at the next
login; a docked captain leaves at once. While a player is away, their fleets follow their standing
orders, their outposts produce, trade and defend, and wages and upkeep fall due. The digest on their
return says what happened. Time runs while the server runs. An empty server either keeps simulating
at ledger fidelity or pauses; that is a setting, and its default is still open (§19, Q4). After
downtime, the ledger catches up on the elapsed time, up to a cap, in one closed-form step per system
(§13.5).

A server's owner sets the rule set, whether the arc runs and the era's length, economy speed, the
maximum number of players (up to 16), the whitelist, logoff timers, vulnerability windows, the loot
fraction and naming rights, all of it as tuning data (§16, R24). A console and in-game commands cover
kick, ban, save, backup, broadcast and inspection, for three roles: owner, administrator and
moderator.

## Part B: The technology

## 12. Six rules

Six rules hold the technical design together. §16 turns five of them, with three more, into the
proposed conformance rules R18 to R25. The fifth, the star system as the unit, is architecture
rather than something a reviewer can check.

1. **The server owns the world.** Clients ask for changes and render what they are told (P1, P7).
2. **The tick is the clock.** Simulation code never reads frame time or wall time (P4).
3. **Seeds for looks, the server for facts** (P6). A client may generate from a seed whatever only
   changes what the player sees: hulls, nebulae, planet surfaces, asteroid shapes. Whatever can
   change the outcome of play comes from the server: an item's statistics, a socket's position, a
   collision shape, a price, a name that is already in use.
4. **Presentation never feeds back.** No simulation state depends on a camera, a draw, a renderable, a
   GPU result or audio (P5).
5. **The star system is the unit** of simulation fidelity, network interest, persistence, threading
   and law (P3). Systems interact only through jumps, which happen between ticks, so each live
   system can be simulated, sent, saved and scheduled on its own.
6. **Every player verb has an AI path.** A verb is not done until an AI agent can use it through the
   same command (pillar 1).

## 13. Target architecture

### 13.1 Authority

| Model | What it needs | Verdict |
|---|---|---|
| Lockstep: every client simulates everything from shared inputs | a bit-identical simulation on every machine | **Rejected.** `lt` builds at `/fp:fast` (ADR-001), clients are x64 and ARM64 (ADR-006), and asteroid fields come from the GPU (ADR-009). Every client would also hold the whole world, which in open PvP is a map hack. |
| Peer authority: each client owns its own objects | trust in clients | **Rejected.** In open PvP (G2), every client is an adversary. |
| **Authoritative server, replicated state** | a server that runs without a GPU; bandwidth | **Proposed (P1).** Clients receive only what they may see. |

### 13.2 Processes and roles

`launch.exe` gains a role. The **client** role, the default, is today's window, renderer, audio and
LTSL interface, plus a network session. The **server** role (proposed flag `--server <world>`) has
no window, no Direct3D device and no audio. It runs unattended, so no dialog can appear, as
`--frames` already arranges (`OS_SetUnattended`, `launch.cpp:47-49`), and it has a console and a
log. For single-player and hosting, the client starts the server role as a child process, bound to
localhost or, for hosting, to a public port, and then connects to it like any other client. A
dedicated server runs the server role alone, on a PC or a virtual machine. The server does not run
inside the client's process because the engine keeps one world per process (§3, point 5).

One executable keeps deployment simple, and `lt.dll` is the same in both roles. It still imports
Direct3D 12, DXGI, XAudio2, DirectWrite and WIC through NeuronClient, all of which ship with desktop
Windows 10 and 11. Whether it loads on Windows Server Core, which lacks some graphics components, is
unverified; delay-loading those DLLs is the fallback.

### 13.3 Layers, and where new code lives

```
launch.exe      client role | server role (--server)                    legacy host (ADR-001)
    |
lt.dll          world, objects, rules, LTSL, replication, saves          legacy engine (ADR-001)
    |-- NeuronClient.lib   graphics, glyphs, images, sound, window, input   governed (ADR-005)
    '-- NeuronNet.lib      sockets, packets, channels, connections          governed, new (P10)
```

**NeuronNet**, with its tests, is the only new project, and it is game-agnostic in the way
NeuronClient is: namespace `Neuron`, with no `Object`, no `String` and no liblt types (AGENTS.md
R9). Its shape is AGENTS.md's own worked example for R2: a `Transport` concept, a `UdpTransport`
over Winsock, which is part of the Windows SDK and so adds no dependency (R14), a
`LoopbackTransport` for tests, and a link conditioner
that adds latency, loss and jitter. It links into `lt.dll`, so it builds wherever `lt` does: x64 and
ARM64, with `lt`'s `/arch`, as ADR-006 requires of NeuronClient. `Build/CheckProjectFiles.py`'s list
of ARM64 projects grows by NeuronNet and its tests. AGENTS.md governs it in full, and NeuronNetTests
carries the placeholder `SuiteSmoke` until its first real test (AGENTS.md §3).

**Everything else lives in `lt.dll`**, for three reasons. Reflection storage differs between
`launch.exe` and `lt.dll` ("a major design flaw in the Type system", `launch.cpp:102-107`), so code
that walks reflected types to save or replicate them must run inside `lt.dll`. New components,
tasks and events can only be C++ inside `lt`, because `TaskT` is an abstract C++ class
(`Game/Task.h`) and components are compile-time mixins. And the interface scripts reach new systems
through the script API that `lt` registers. New code in `lt.dll` follows liblt's idiom under
ADR-001, is held to R18 to R25 (§16), and marks every override `override`: three tasks carry the bug
that keyword would have caught (Appendix A2). Holding new liblt files to more of AGENTS.md is the
owner's call (§19, Q8).

### 13.4 The simulation core

This is M0's work. None of it is networking, and all of it must come first.

**(a) The tick.** The engine owns the loop, and apps stop calling `system.Update dt`. In the server
role, each tick takes the commands that have arrived, advances each live system by a fixed step,
advances the ledger (§13.5), and builds the snapshots. The proposed rate is 30 Hz, to be measured on
a busy system before an ADR states it, with 20 Hz as the fallback (§13.13). Wall time meets ticks
only in the server's pacing loop, which is the seam AGENTS.md R16 names. `UpdateState.dt` becomes
the fixed step, and `Universe::age` advances by it. Behaviour that counts updates becomes a rate per
second; beams, for example, retarget every other update, and colonies roll 1% per update
(`Object/Colony.lts:10-17`). Time acceleration (H) and pause (P) become server features that change
ticks per wall second, for scenarios and single-player, and never change the step.

**(b) Presentation leaves the simulation.** The server builds and runs only simulation state.
Everything below moves to the client, which builds it lazily at first draw and never on the server:

| In the simulation today | Becomes |
|---|---|
| A system's nebula, starfield, IR map and colour grade (`Game/Object/System.cpp:81-114`) | the client's presentation of the system, built from its seed |
| Planet surfaces and rings, made on the GPU (`Game/Item/PlanetType.cpp`) | client presentation, from the seed |
| The hull's occlusion bake on the GPU (`Item/ShipType/Generate.lts:89`, `LTE/PlateMesh.cpp:112`) | client-only. The hull's *geometry* is still built on the CPU in both roles, because sockets are ray-cast against it (`Game/Item/ShipType.cpp:223-259`). |
| Bounds read from the renderable (`Game/Object.cpp:165-166`) | bounds stored on the item type, computed once from its geometry |
| Collision meshes from renderables (`Module/PhysicsEngine.cpp`) | for hulls, from the CPU geometry; for asteroids, from a proxy (below) |
| Asteroid fields on the GPU, with shapes that differ per run (ADR-009; `Game/Renderable/Asteroid.cpp:17`) | visuals as ADR-009 decides, but seeded from each asteroid's own seed. Collision and mining use a convex proxy that the server computes from the same seed and scale. |
| Zone asteroids created and deleted while drawing, around the camera (`Game/Object/Zone.cpp:62-100,119-124`) | **gameplay asteroids**, finite, mineable and persistent, placed when the era is created; and **a decorative field** that follows the camera on the client only and has no effect on play |
| Sounds, lights, explosions, particles and trails spawned by simulation code and object scripts (`Game/Item/WeaponType.cpp:89-117`, `Object/Ship.lts`, `Object/WarpNode.lts`) | **effects**: events the simulation emits with a kind, a place or entity, and parameters. The server sends them, and the client plays them. |
| The light children of weapons, pulses and thrusters (`Game/Light.cpp`) | client presentation, which stops consuming entity identities |

Asteroid proxies are the one place where a player can see the gap. A proxy follows the visible
surface closely but not exactly, so a ship scraping a crater may touch rock a hand's breadth early
or late, and that is accepted. The alternative, a coarse collision field built on the CPU, needs
Worley noise in C++ (`FractalWorley` is `NOT_IMPLEMENTED` on the CPU, `LTE/SDF.cpp`) and an
amendment to ADR-009, which forecloses CPU fields. The ADR for this step chooses between them.

**(c) Identity.** An `EntityId` is 64 bits, allocated by the server from a counter saved with the
world and never reused within an era. Only simulation objects have one, and a registry maps each id
to its object on the server and to its replica on the client. What crosses a boundary (the save, the
wire, a reference between systems) is an id, never a pointer (R22). Destruction is real: a destroyed
object leaves the registry and its container, and references held by id resolve to nothing instead
of keeping a zombie alive. Item types become canonical. Today the same generator arguments give two
distinct items with two ids (`Game/Item.cpp`); a registry keyed by kind and arguments makes them one
`ItemTypeId`, the same everywhere. Cargo, databases, storage and markets key by that id, which also
ends the iteration ordered by memory address (`Component/Cargo.h:10`, `LTE/Reference.h:110-112`).

**(d) Commands.** A client changes the world only by asking. Flight input (thrust, heading target,
boost, fire groups) goes every tick, unreliably and with redundancy. Discrete commands (dock, board a
rail, jump, target, trade, fit a module, fleet orders, chat, administration) go reliably. The server
validates each against ownership, range, cooldown, law, funds and rate, applies it at a tick, and
answers a rejection with a reason. On the client, the script natives that change the world become
commands. Each of the engine's 1,032 natives (ADR-013) must be classed as reading the world, as
presentation only, or as changing the world, and M2 starts with that audit. Today's world-changing
script calls include boarding a rail by sending `MessageStartUsing` (`App/ltheory.lts:66-78`),
placing a market order (`Widget/Market/Transaction.lts:162-181`) and fitting equipment (`Plug`). The
message types are already reflected (`Game/Messages.h`), which makes them a natural command format.
`Handling.lts` splits in two: reading input and moving the camera stay on the client and emit the
flight command, while turning a heading into motion becomes simulation, authoritative on the server
and predicted on the client (§13.6).

**(e) Randomness and replay (P11).** Every random draw in the simulation comes from a stream derived
from the era's seed and the drawing system's id, and generation has streams of its own. `rand()`,
`Rand*`, `RNG_Default` and the effect of `srand(time(0))` (`LTE/Program.cpp:17`) leave simulation
code, although presentation may keep them. The audit found them in weapon cooldowns, beams,
docking, mining, four AI tasks, projects, blueprint derivation, the economy's item pick and asteroid
shapes. Iteration that reaches an outcome is ordered by identity (c), and `FrameTimer_Get` inside
object scripts moves to presentation or to the tick. A server run then replays from its save and
its command log, on the same binary and the same machine. It does not replay across machines, and it
need not: `lt` stays at `/fp:fast` (ADR-001), x64 and ARM64 differ, and on x64 the CRT chooses its
transcendental functions by whether the CPU has FMA3 (`_set_FMA3_enable`). Replay is a debugging and
testing tool, not a network model.

**(f) Threading.** At first, one thread ticks all live systems in turn. Because systems meet only
through jumps that are handed over between ticks, live systems can later tick in parallel as jobs,
but only once no object is shared between systems during a tick, since reference counts are not
atomic. NeuronNet's socket thread handles bytes, never objects.

### 13.5 Fidelity: live and ledger systems

The engine simulates one system today (§4.4). Up to sixteen players in different systems, in a
cluster of tens of systems whose factions never stop, need two levels of detail (P12).

A **live** system is simulated in full at the tick: objects, physics, tasks and combat. A system is
live while a player is in it, for a grace period after the last one leaves, and while it has seen
recent combat. Every other system is a **ledger** system, run as accounts and schedules: the stock at
each market, production rates, each faction's presence and strength, fleets as schedules (origin,
destination, arrival and cargo), and the state of each outpost. The ledger advances on a coarse tick
(proposed: one second of game time), with closed-form progress between ticks, so catching up after
downtime costs one step per system rather than one per second.

A system unfolds into objects when a player arrives. Its persistent objects are already in the save
(§13.7), so only ships in transit and the ledger's quantities are placed. It folds back into
accounts and schedules when its grace period ends, and a fight that cannot end in time auto-resolves
by strength, under the same rules for AI and players, into the Chronicle.

The seam between the two fidelities is where bugs and exploits live; the classic exploit is jumping
out of a losing fight so that it auto-resolves instead. Four things guard it: the grace period, the
rule that recent combat keeps a system live, an auto-resolve that is conservative, and soak tests
that compare the ledger's outcomes with live runs of the same scenario (§13.11).

### 13.6 Networking

**Transport (P10).** NeuronNet runs over UDP. Its handshake checks the protocol version, the build
and content hash, and the player's key, by challenge and signature. An unreliable, sequenced channel
carries snapshots and flight input; a reliable, ordered channel carries commands, results, chat,
Chronicle entries and item-type definitions. It fragments the burst sent on entering a system, and
has simple rate control, keep-alives and time-outs.

Three alternatives are rejected. ENet is small and MIT-licensed, but has no encryption and would be a
vendored dependency (R14); it stays the fallback if the reliability layer costs more than expected.
GameNetworkingSockets brings encryption and relays, but also CMake, protobuf, and OpenSSL or
libsodium, which is heavy for a tree with no CMake. Steamworks brings a relay and an identity, but it
is proprietary and ties distribution to Steam, which is the owner's call (§19, Q5).

**Security.** Each player has an ECDSA P-256 key pair, generated by the client and kept in the
player's profile folder as a runtime file (R13); the server stores the public key with the captain.
Before any internet PvP server exists (M3), session keys are agreed by ECDH and packets are sealed
with AES-GCM, both from CNG (`bcrypt`), which ships with Windows and so adds no dependency.

**Replication (P7).** A client holds a replica world made of the engine's own objects, so the HUD,
map, inspector and market keep calling the same `Object` API. Replicas are puppets: their tasks,
economy and AI do not run, only interpolation and presentation. On entering a system, the client
receives in one reliable burst the static state (stations, rails, planets, asteroids) and any
item-type definitions it lacks. After that it receives one snapshot per tick, delta-encoded against
the last snapshot it acknowledged, in the manner of Quake 3, which tolerates loss without per-field
reliability. What replicates is declared per component, with fields, precision and rate, rather than
"every reflected field". That keeps bandwidth predictable and hidden state hidden: another captain's
cargo and orders are never sent unless they are scanned.

A client receives the objects of its own live system only, prioritised by distance, size, threat and
targeting, and far objects update less often when it is over budget. Its assets elsewhere, the
cluster map and the Chronicle reach it as summaries on the reliable channel. Once scanners work,
**detection is relevance**: a client receives what its sensors detect, plus what is always visible
(stars, stations, rails), so a cheat that reveals everything reveals only what the player could see
anyway. Effects travel unreliably, stamped with their tick. Positions are doubles in each system's
own frame (`LTE/Common.h:59`), and the renderer already works relative to the camera. On the wire,
positions are quantised relative to a sector origin, so precision holds from a rail node to the
system's edge.

**Prediction.** The client predicts only its own ship, running the same flight code at the same step,
replaying unacknowledged inputs on each correction and smoothing what is visible. Everything else is
interpolated between snapshots, about 100 ms behind (proposed). Projectiles are spawned by the
server and flown by clients from the spawn event, and the hits are the server's to decide. Beams are
hitscan and lag-compensated: the server rewinds the targets by the shooter's latency, up to a cap
(proposed: 200 ms), and tests the hit there.

**Bandwidth** (estimates, to be measured in M2). A busy live system has about 40 moving objects. At
about 24 bytes each per snapshot before compression, and 30 snapshots a second, that is about
29 KB/s per client, which delta encoding and relevance usually cut to 5 to 10 KB/s. A host with 15
remote clients would then upload 0.6 to 1.2 Mbit/s, which a home connection carries.

**Reaching a server.** The first release supports LAN and direct connection to a forwarded port. A
relay or rendezvous service comes later, and depends on distribution (§19, Q5).

### 13.7 Persistence

**Generate, then own (P8).** When an era is created, the server runs the generators once (LTSL's
`Object/System:Init` and the item-type generators) and saves the result. From then on the save *is*
the world. Generators may change from build to build without disturbing existing eras, and seeds are
kept per object for presentation only. Seed-and-delta is rejected: it would re-read every stored
delta against generators that change constantly during development, and corrupt worlds silently.

The mechanism exists. The engine's reflection serializer walks object graphs by id, restores derived
types by name, and is versioned (`LTE/Serializer.h:15-42`); the settings file already goes through
it (`Module/Settings.cpp:39-45`). For world saves it runs over a whitelist of persistent types,
refuses unreflected types instead of copying their raw bytes (`LTE/Type.h:432-440`), never saves
presentation (`Component/Drawable.h:10`), and runs inside `lt.dll` (§13.3).

A saved world is one chunk per system, plus a cluster file for factions, players, the ledger, the
arc's state and the entity and item-type counters, plus the Chronicle as an append-only log. A
manifest names the chunk versions. It is written, flushed and renamed into place, which makes every
save crash-consistent. Saves happen at tick boundaries, with each system's chunk written in its own
slice so that a save never stalls a tick, a full save at shutdown, and an autosave on an interval
(proposed: 5 minutes). Backups rotate manifests and chunks, and an administrator can take one on
demand. Each save carries its format version, and loading runs the migrations in order; the
serializer's version bounds already exist (`LoadFrom`'s minimum and maximum). A captain's record,
keyed by public key, holds the name, standing, assets by id, stats, titles and berth. Worlds live in
a folder named by the server's configuration, by default `saves/<world>/` beside `GameData/`, where
ADR-004 already puts `cache/`; that needs an ADR (R13).

### 13.8 Scripting

LTSL keeps four jobs (P9): the interface, the tools (`model`, `platemesh`), content generation
(`Object/System:Init` and the hull generators), and object behaviours attached with
`Object_AddScript`, which split into a simulation half and a presentation half (§13.4b). It runs in
both roles: the server runs generation and the simulation halves, and the client runs the interface
and the presentation halves. The rules of play are C++. New components, tasks and events can only be
C++ anyway (§13.3), and C++ keeps the rules in one place with the reflection and the saves.

The game becomes one app with states (front end, loading, in the world) instead of one app per
process. Today's apps become **scenarios**, a seed plus a bootstrap script that the server role can
load, and `war`, `dogfight` and `rails` become the first headless tests (§13.11). The server fails
fast. Today, a statement that fails to compile is dropped with a log line while the script runs on
(`LTE/Expression.cpp:141-149`), and a failed call at run time shows a message box and exits
(`LTE/Expression/DynamicDispatch.cpp:51-78`). In the server role, a compile failure stops the server
at start-up, and no dialog is ever shown.

Lua is not proposed. The newer Limit Theory moved to it, but here it would be a new dependency and a
port of 9,274 lines. It is worth revisiting only if LTSL's gaps (no recursion, no closures, poor
errors) become the bottleneck. For mods, the client's content hash must match the server's at the
handshake, and the first release pushes no content.

### 13.9 Client front end and interface

The front end needs a main menu (continue, new world, host, join, settings, quit), captain creation,
a server list (LAN, favourites, direct address), a loading screen reusing the `loading` app's, and a
game menu whose buttons work (`Widget/GameMenu.lts:4-8`). The settings window is rebuilt: it calls
`WidgetSettings`, a native that ADR-013 removed (`Widget/Settings.lts:13`), and its only caller is
commented out (`App/widget.lts:28-41`). `Module/Settings.cpp` gets setters and a script API, and
NeuronClient's backlog becomes necessary: borderless fullscreen, resolution and vsync (ADR-007,
ADR-012). Input moves to a C++ input map whose script API names actions rather than keys, with
rebinding saved as a runtime file (R13); it replaces the Button/Axis system that ADR-013 removed.
Joysticks stay out unless decided otherwise (ADR-012).

The new panels are the contract board, a cluster map (today's map shows one system), fleet and
orders, construction and production, faction and diplomacy, law indicators (the HUD's sovereignty is
hard-coded "Unclaimed", `Widget/HUD/Container.lts:37`), the Chronicle's views, chat and the player
list, and the digest. Some widgets rebuild every frame (`Dynamic` re-runs `CreateChildren` and diffs
the result, `UI/Widget/Dynamic.cpp:21-56`), so the interface's cost is measured before new panels
multiply it (§13.11).

### 13.10 Audio and music

Sounds stay as ADR-003 decides, and the client plays them from effects. Nothing plays music today,
because FMOD's adaptive music went with ADR-003. The proposal is music as MS-ADPCM WAV, which the
engine already reads (ADR-013, decision 5) at a quarter of the size of 16-bit PCM, so music needs
neither a decoder nor an amendment to "sounds are WAV files". Music is layered by state
(exploration, tension, combat, docked), and the client cross-fades the layers. The alternative is
Media Foundation, which ships with Windows and decodes compressed formats, but it is absent from the
N editions of Windows sold in Europe unless the Media Feature Pack is installed, and it would amend
ADR-003.

### 13.11 Tooling, testing and CI

The perf-review skill's Phase 0 baseline has never been taken. Take it before M0 changes the loop,
so that the effect of the tick and of the split is measured rather than guessed.

Once M0 lands, the server role runs scenarios without a GPU, and the game runs in CI again for the
first time since the smoke job went (N20). Each scenario runs a set number of ticks and checks that
no assertion fires, entity counts stay bounded, the money supply stays in its band, and saving,
loading and continuing reaches the same state hash as an uninterrupted run. NeuronNetTests cover the
transport over loopback and over localhost UDP, with the conditioner. A headless client role driven
by the same AI brains supplies load, so symmetry pays twice. A save plus a command log replays on the
same binary, and a state hash every N ticks finds where two runs diverge. At M4, an empty server
soaks for 72 hours with memory and handles flat; the migration's 21-minute soaks, which held about
915 MB of private memory, are the precedent (`Design/Archive/NeuronClient-migration.md`, Phase 5).
The server writes counters to a CSV log, a runtime file (R13): tick time per live system at p50, p95
and p99, entity counts, bandwidth per client, the money supply and its flows, and the Chronicle's
rate.

### 13.12 Operations

Dedicated servers run on Windows x64, as a console application or a service, on a home PC or a cloud
VM. The engine is Windows-only today (liblt's Linux branches are backlog, NeuronClient-migration
§11), and because the server role never touches NeuronClient's device, a later Linux port stays
possible. Each world has one configuration file, holding the settings of §11 (R13). In the first
release, client and server must run the same build and content, and the handshake refuses a mismatch
and says why. Updates are save, stop, update, start, with saves migrating forward (§13.7). Logs and
crash dumps go under the world folder rather than `cache/`, so each server's logs stay with its
world.

### 13.13 Budgets

These are proposals, and each is measured before it goes into an ADR (AGENTS.md §6). The reference
machine is the owner's to name (§19, Q10).

| Budget | Proposed | Measured in |
|---|---|---|
| Server tick for the busiest live system, on the reference machine | p99 at most half a tick: 16 ms at 30 Hz | M0 |
| Live systems per server | 16, one per player | M3 |
| Snapshot bandwidth per client | 16 KB/s on average | M2 |
| Save slice per tick | 2 ms | M1 |
| Time to enter a system, on a GPU client | 5 s | M3 |
| Empty-server soak | 72 h with memory flat | M4 |

## Part C: The plan

## 14. Phases

Every phase ends in something that can be played or run, and its done-when says who judges it. M0 to
M3 make a multiplayer game you can play with friends, M4 makes it *this* game, and M5 to M7 climb the
player's arc. The foundations are never cut; when time runs short, cut from the end. Before M0, run
the perf-review skill's Phase 0 on the owner's GPU, so that M0's effect on load and frame time is
measured.

| Phase | Delivers | Done when |
|---|---|---|
| **M0** A world without a window | The fixed tick, the presentation split, identity, seeded randomness and identity-ordered iteration (§13.4). Every system in a world ticks, at full fidelity for now, and the universe's age advances. The server role loads a scenario headless. | The worlds of `war` and `ltheory` run 100,000 ticks in the server role, with no Direct3D device, in CI. Two runs of one scenario on one machine end with the same state hash. The client role still draws them as before, which the owner judges by eye. |
| **M1** Keep the frontier | World saves, with migrations and backups (§13.7). A minimal front end: continue, new world, quit. A working game menu. The ADR for runtime files. | Quitting and resuming show no difference. A save made before a format change loads after it. A crash during a save loses no more than the last autosave interval. |
| **M2** Client and server | NeuronNet. The audit of natives, and the commands (§13.4d). The replica world, snapshots and deltas, prediction and effects. Single-player through a local server process. | The owner flies an `ltheory`-like world through the local server at 0 ms, and with 100 ms of latency and 1% loss from the conditioner, and judges the handling the same at 0 ms and acceptable at 100 ms. Bandwidth is measured against §13.13. |
| **M3** Friends in the Marches (T0) | Up to 16 players, joining by LAN or direct address, with chat and a player list. Jumps between systems: Goto's missing branch and the handover between live systems. Combat completed: death, pods and respawn; shields and power; beams, scanners and collision. The first version of law: Chartered and Lawless space, the Authority's response, bounties, the outlaw flag. The player's economy: dock, sell, buy, refit, repair. Encryption, the dedicated server's configuration, administration and autosave. | Four players play for two hours on a dedicated server over the internet, and the server restarts in the middle with everyone resuming where they were. Headless bot clients (§13.11) hold 16 connections within the budgets of §13.13. |
| **M4** The living frontier | Ledger fidelity, with folding and unfolding (§13.5). The economy of §9.3, instrumented: the mint, consumers, production chains, transport that carries goods, market makers and contracts. Faction brains, Held space, the Chronicle's first version, and the market fixes (Appendix A1). | An empty server runs for 72 hours. Faction territory and prices have moved within their designed bands, the money supply stayed in its band, and the Chronicle reads as a history the owner recognises. |
| **M5** Captain and commander (T1, T2) | Several ships per captain, NPC captains, standing orders and wings, the fleet panel, and the offline digest. | A player runs a profitable trade route and a mining operation while offline, and the digest explains every loss. |
| **M6** Founder (T3) | Station kits and modules, production, research and blueprints, market administration, vulnerability windows, and player contracts. | Two players found an outpost in Held space, supply it, defend it through one attack in its window, and build a ship from their own blueprint. |
| **M7** Flags and the Tide (T4, and the story) | Player factions, with claims, law, diplomacy and war. The Director, the era arc, the first hour, titles, and the story so far. | A test server plays an accelerated era from Landfall to the Epilogue, and a newcomer who joins in Act III understands the story from the briefing alone. |
| **M8** Release | Settings, rebinding and display modes, music, administration tools, crash reporting and packaging. | Every budget in §13.13 holds on the reference machine, in Release. |

## 15. ADRs to write

Each ADR is numbered when it is written, in the commit that implements it (AGENTS.md §6), so none is
reserved here.

| Phase | ADR | Settles |
|---|---|---|
| M0 | Authority and roles | P1, P2 |
| M0 | The simulation tick | P4, with the measured rate |
| M0 | Simulation and presentation | P5 and P6, and asteroid proxies or an amendment to ADR-009 |
| M0 | Entity and item-type identity | §13.4c |
| M0 | Randomness and replay | P11 |
| M1 | World saves: format, place and versions (R13) | P8 |
| M2 | NeuronNet: transport over Winsock, its platforms and its tests | P10 |
| M2 | Replication, prediction and commands | P7 |
| M2 | LTSL's role, and scenarios | P9 |
| M3 | Player identity, and encryption through CNG (R13 for the key file) | §13.6 |
| M3 | The dedicated server: platform, configuration and runtime files (R13) | §13.12 |
| M4 | Live and ledger fidelity | P12 |
| M8 | Music | §13.10 |

Game rules (law, loot, prices, the arc) are design, not engineering. They live in this document and
in tuning data, not in ADRs.

## 16. Proposed conformance rules

AGENTS.md reserves R18 onward for "conformance rules that come from a project's design (which state a
routine may read, what an event must carry, where tuning values live)". These are proposed for
AGENTS.md §5. They are to be written there once the owner accepts this document, and not before.

- **R18: The server owns the world.** Authoritative state changes only in the server's tick, through a
  validated command or a simulation rule. A client never changes it; it asks.
- **R19: The tick is the clock.** Simulation code reads the tick and the fixed step. It never reads
  the frame timer or the wall clock, and never counts updates. This extends R16's rule for
  deterministic components to the whole simulation.
- **R20: Seeds for looks, the server for facts.** A value that can change the outcome of play is
  computed by the server and replicated. A client computes from a seed only what is presentation.
- **R21: Presentation never feeds back.** No simulation state may depend on a camera, a draw, a
  renderable, a GPU result or audio.
- **R22: Identity crosses boundaries; pointers do not.** Whatever is saved or sent names objects by
  `EntityId` and item types by `ItemTypeId`.
- **R23: Every player verb has an AI path.** A verb is done when an AI agent can use it through the
  same command.
- **R24: Tuning lives in data.** Rates, prices, timers, ranges, drop chances and thresholds live in
  the server's tuning data, in one place. They are not literals in C++ or in scripts.
- **R25: Events say who, where and when.** Every simulation event that may reach the Chronicle carries
  its participants' identities, its system, its tick and its importance.

No checker could enforce R18 to R21 from source text, so until one exists they are review's problem
(AGENTS.md §1, Enforcement). M0's headless CI run enforces the heart of R21 anyway: a server with no
device cannot draw.

## 17. Risks

1. **The presentation split is larger than it looks.** GPU and mesh coupling reach into
   constructors, scripts and components (§4.4, point 1). Do it first, measure it by the server role
   running with no device, and work one object type at a time.
2. **The living frontier is new code and hard to balance.** The economy may die or run away, and the
   AI may be dull or dominant. Instrument the money supply from M3, soak-test, keep faction
   behaviour in tuning data, and use the Director as a pressure valve.
3. **Open PvP at small populations drives players away** (§3, point 3). The six mechanisms of §9.2
   apply, with the Lawful rule set as the fallback. The measure is whether the owner's own group
   keeps playing on a test server.
4. **LTSL on a server** drops statements that fail to compile, dies on errors at run time, and has no
   recursion. Fail fast at start-up, run unattended, and keep the rules in C++.
5. **Performance.** The main loop is single-threaded, the economy's per-update cost grows with the
   square of its markets, CI builds Debug only, some widgets rebuild every frame, and a server may
   have 16 live systems. Take the baseline before M0, hold the budgets, and tick live systems as
   jobs later (§13.4f).
6. **Saves break during development.** Generate-then-own and migrations contain it, and test servers
   accept era resets.
7. **New code in exempt liblt drifts from any standard.** R18 to R25, `override`, and tests in the
   server role hold it, with an optional ADR that narrows ADR-001 for new files (§19, Q8).
8. **Internet servers without encryption invite hijacked sessions.** Encryption lands before public
   servers, in M3.
9. **Scope.** Five tiers and a story, for a small team. Every phase ends in something playable; cut
   from the end, never from the foundations.
10. **Windows-only servers cost more to host in the cloud than Linux ones.** Accept that for the first
    release. The server role never touches NeuronClient's device, so a port stays possible.

## 18. Not in this design

- Walking avatars, ship interiors, planetary landings.
- More than 16 players on a server, sharding, or travel between servers.
- Voice chat, an internet server list, matchmaking.
- Consoles, mobile, macOS, a Linux client.
- Modding in multiplayer beyond matching content hashes.
- Lockstep, or determinism across machines.
- Anti-cheat beyond server authority, validation and relevance.
- A different graphics API (ADR-007 stands) or sound engine (ADR-003 stands).
- Monetisation.

## 19. Still open for the owner

| # | Question | Blocks |
|---|---|---|
| Q1 | **Names and tone.** Keep or replace the Marches, the Gate, the Tides and the Charter Authority? A grounded, industrial tone, or pulp? | M7's text; nothing earlier |
| Q2 | **The era.** Its default length, and whether the arc runs by default or is opt-in. | M7 |
| Q3 | **PvP defaults.** Frontier or Lawful as the default rule set, and the loot fraction on death (proposed: 50%). | M3 |
| Q4 | **Empty servers.** Does a server with nobody online keep simulating? That trades the cost of an always-on machine against "the frontier lives without you". | M4 |
| Q5 | **Distribution.** Standalone or Steam? Steam brings a relay for NAT traversal and an identity, and is a dependency (R14). | M3's reach beyond LAN and direct connections |
| Q6 | **Server platform.** Windows x64 only, for the first release? | M3 |
| Q7 | **Scripting.** LTSL for the interface and content with the rules in C++, or rules that mods can change? | M2 |
| Q8 | **New liblt code.** Hold it to R18 to R25 only, or narrow ADR-001 so that new files follow more of AGENTS.md? | M0 |
| Q9 | **The old apps.** Keep them as test scenarios, or retire them once the game flow exists? | M1 |
| Q10 | **The reference machine** for the budgets in §13.13. | M0's measurement |

## Appendix A. Defects found while surveying

None of these is fixed here, because this document changes no code (AGENTS.md §6: stay in scope).
Each is its own change, and most land naturally in the phase named.

| # | Defect | Where | Effect | Phase |
|---|---|---|---|---|
| A1 | A partly filled bid is refunded `bid->price × bid->volume − totalPrice` before its volume is reduced, while the bid stays open. The refund should be (bid price − trade price) × trade volume. | `Component/Market.cpp:113` | Creates credits on every partial fill below the bid. | M3 |
| A2 | `Pirate`, `Produce` and `Spawn` declare `GetOutput`, which does not override `TaskT::GetOutputs`. | `Game/Task/Pirate.cpp:62`, `Produce.cpp:56`, `Spawn.cpp:28`; `Game/Task.h:56` | The AI never sees what these tasks produce, so it never chooses them. | M4 |
| A3 | `CanDock` returns `docked >= capacity`. | `Component/Dockable.cpp:15-16` | Inverted. | M3 |
| A4 | `Traits` lists seven traits and declares `XSIZE 6`. | `AI/Traits.h:4-15` | Every `Vec` operation (`LTE/Vec.h` loops to N) skips `Sociable`, while reflection sees all seven. | M5 |
| A5 | `Universe::age` is never advanced. | `Game/Universe.h:26`, `Game/Universe.cpp:36-39` | `Universe_Age()` is always 0, so log entries and trades carry no time, and knowledge never expires. | M0 |
| A6 | The mining task increments `index` and then reads `offsets[index]`. | `Game/Task/Mine.cpp:112-118` | Reads one element past the end. | M4 |
| A7 | `Event_Destroyed` is declared and never defined. | `Game/Events.h:15` | Nothing records a destruction as an event. | M4 |
| A8 | Each hit adds a newly generated `Item_Data_Damaged` item to the attacker's store. `ComponentIntegrity::GetDataDamaged` caches one per object, and `Damage.cpp` does not use it. | `Game/Event/Damage.cpp:39`, `Component/Integrity.cpp` | By reading, since stores key items by address: the attacker's store grows with every hit. | M3 |
| A9 | Asteroid models are seeded by `Rand` after `srand(time(0))`. | `Game/Renderable/Asteroid.cpp:17` | Shapes and collision differ from run to run. | M0 |
| A10 | Blueprint derivation uses `RandExp` and unseeded `rand()`. | `Game/Item/Blueprint.cpp:48-70` | Research cannot be reproduced. | M6 |
| A11 | ADR-004 lists cached script results under `cache/cache/`, but script caching is compiled out. | ADR-004, decision 3; `LTE/ScriptFunction.cpp:6-29` | The document has drifted from the code. | now |
| A12 | The perf-review skill says the GPU field time "has not been taken", but ADR-009 records it (20 to 23 ms on an Intel Iris Xe). | `.claude/skills/perf-review/SKILL.md:429-430` | The document has drifted from the code. | now |
