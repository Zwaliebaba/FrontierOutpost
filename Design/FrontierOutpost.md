# Frontier Outpost

**Game design · Revision 4 draft · 10 November 2026 · Proposal for owner review**

Revision 4 responds to the review in [reviews/2026-11-10-revision-3-review.md](reviews/2026-11-10-revision-3-review.md). Text applies from Stage 1 unless it is tagged **[S2]**, **[S3]** or **[S4]**, the stage in §9 at which it first applies. **Open:** marks a decision that has not been taken.

## 1. Identity and scope

**Establish and enhance your outpost. Equip ships for dangerous expeditions, bring discoveries home, and expand the facilities that determine what you can build and where you can go.**

Frontier Outpost is an isometric space game. Its signature is a visibly evolving outpost whose layout, upgrades, and specialization determine expedition capabilities. A trailer should show ships returning with salvage, a new facility transforming the outpost, and the launch of a newly possible mission. Exploration, combat, and trade support this experience.

**Freedom is choice with consequences, not a seamless open world.** Players decide what the outpost becomes, what to fly, which opportunities to pursue on each expedition, and when to leave. In Stage 1 every session includes an expedition: the outpost decides what is possible there, and each run offers more than one ship can take. **[S2]** Trade allows specialization; nobody must personally gather, manufacture, and fight to progress. Solo players and crews of 1–4 are the initial audience.

**Build an expedition game with persistent homes first.** Shared regions are a gated expansion. A full MMO depends on demonstrated population density, technical capacity, and sustainable operations. If those gates fail, the smaller game remains a complete product. A continuously populated shared world is a Stage 4 possibility, not a commitment. Familiar space-economy features alone do not differentiate it from EVE Online.

## 2. The home and the expedition

**Configure home → prepare → explore, extract, or fight → return → trade and develop → choose a new objective.**

| Activity | Meaningful decisions | Contribution |
|---|---|---|
| Outpost development | Which facilities to build and power, upgrades, reconfiguration, what to process, sell or keep; **[S2]** contracts | Expedition preparation, processing, services |
| Exploration | Which pockets to enter, scanning, hazard interpretation, when to leave; **[S2]** access-key use | Access to finite opportunities, salvage, resources |
| Combat | Piloting, positioning, targeting, module timing, retreat | Protects extraction and transport, contests opportunities |

Players manage the outpost from an isometric overview, selecting facilities to build, upgrade, specialize, or reconfigure. Authored external modules attach to defined construction sockets; Stage 1 uses a fixed set of sockets with no free placement. Limited power, attachment points, and functional adjacency create choices. **Every facility opens or changes an expedition option:** a pocket it admits the crew to, a scan, a rig or a module. It may also change a home-side rate, but none is only a rate. For illustration: a sensor array reveals a signal pocket; a refinery lets a crew work volatile veins as well as improving processing; allocating a bay to repair reduces fabrication capacity. Expansion adds options without eliminating constraints.

Unlocks are permanent. The active layout is bounded by power and sockets, and there are always more facility options than sockets. Reconfiguring consumes installation materials, so a layout is a commitment for several runs, not a loadout screen.

Enhancements change the outpost's silhouette, visible equipment, and operational capabilities. Each facility shows its state in the overview (empty socket, building, powered, unpowered, producing, blocked), and each tier changes the outline enough to read at the zoom where the whole outpost fits. Upgrade previews show resource costs, power demand, and the capability gained, naming the pocket, scan or rig it enables. Specialization branches offer alternatives rather than an endless ladder of larger numbers. Facility selection opens concise management panels; there is no avatar movement or explorable interior. Docked ships and visible production activity give the home presence. **[S2]** Visiting crews add social presence; in Stage 1, crewmates' ships appear docked from roster data.

Everyone receives a starter outpost: a starter ship with a saved basic fitting, and one empty socket. The first completed run should fund one facility, installed at once, that visibly changes the outpost and opens one new option on the next run. Deep customization is optional. Processing advances by the server clock, online or away, at the pace of the powered layout (§10). Unfinished batches can be taken back for their inputs; work on the batch in hand is lost.

**The Stage 1 expedition** is one authored site, offered through an NPC discovery contract (§4). The site has three or four pockets, such as an extraction field, a wreck, a hostile nest and a hazard pocket. Cargo, heat and time prevent clearing them all in one run, and some pockets need a facility or a rig. A seed varies the hazard type, where the best yield sits and the threat mix. Preparation shows part of this; scanning in the site reveals the rest. The contract offers the site at two or three threat tiers, and the lowest is completable in the basic fitting. Per-run yield is finite and threat escalates with time and heat, so when to leave is a decision with a cost. Cargo counts only when it is docked at home.

**Travel** consists of approach and return legs flown inside the expedition instance. There is no separate transit scene. Return risk rises with cargo carried. Travel time is measured from undock to the first pocket and from the last pocket to dock.

Two drives shorten the legs without removing that risk. The *cruise drive* multiplies speed outside the site, but not while a hostile has the ship locked or the extractor is working. The *jump drive* moves the ship to the dock or a pocket after a short spool: the pilot chooses a destination on the system map and engages it. A jump is made from open space: the drive does not spool until the ship has flown clear of the dock. The map shows only the site being flown, the dock and its pockets; a map of several sites stays outside Stage 1 (§9). The scene itself carries no labels for places: a minimap gives their bearings, and the destination chosen is marked by one waypoint. A hostile lock or a working extractor refuses or cancels the spool. A jump to a pocket arrives outside it, facing it, so the pilot sees what guards it before entering. A jump home with cargo arrives short of the dock and outside the site, so the return-leg interceptors still stand between the ship and the berth. Docking is always flown.

**Session budget:** target 30–60 minutes, with ordinary outbound and return travel together at most 20% of session time. A 30-minute example allocates 4 minutes to preparation, 6 to travel, 16 to the objective, and 4 to unloading and decisions. A first expedition should reach its first decision within about two minutes of launch. Longer expeditions are explicit commitments. **[S3]** Frontier staging reduces travel while exposing supplies and position.

**Crews.** A crew of 1–4 forms by invite code. The host chooses the contract and the threat tier. Each pilot launches from, and returns to, their own home in their own ship. Access is shared: if any crew member's outpost opens a pocket, the whole crew may enter. Rigs and modules stay tied to each pilot's own outpost. Cargo belongs to the ship that docks with it. Contract payouts are per expedition, split equally by default; the host may change the split before launch and every pilot sees it. Players cannot transfer items or currency to each other in Stage 1. Threat scales less than proportionally with crew size and never with experience. The site includes at least one optional objective that rewards two ships acting in different places at once; nothing essential requires it.

## 3. Progression and useful newcomers

Stage 1 has one starter hull with three role rigs: scout, extraction and escort. A basic solo fitting combines functions less efficiently, so essential activities never require a full crew. **[S2]** Players develop a hangar of specialized ships rather than one indefinitely superior vessel.

**[S2]** Small hulls have intrinsic capabilities that skill progression cannot buy for large hulls: low signatures, narrow wreck access, and entry into mass-limited hazard pockets. Larger ships gain capacity or endurance but retain weaknesses. Veterans still have reasons to fly inexpensive ships. This claim is untested until a second hull size exists.

In Stage 1, progression comes from facilities, which unlock rigs, modules and pockets; there is no separate pilot progression. **[S2]** Pilot progression primarily unlocks fittings and specialization. Numerical proficiency bonuses have explicit ceilings and cannot replace role choice or piloting. New players should reach one useful role in their first session: solo, that means returning with cargo from the primary objective; in a crew, it means doing something the crew would otherwise have gone without. **[S2]** Veterans pursue difficult sites, outpost configurations, production businesses, and collective objectives; in Stage 1 they pursue higher threat tiers and outpost configurations.

**A newcomer's first contribution is a second attentive pilot.** A veteran can own every rig, so newcomer value cannot rest on equipment. It rests on being in a second place at the same moment, and, after the newcomer's first facility, on an outpost that may open something the crew's other outposts currently do not.

Support is active play. Extraction balances heat, yield, and changing hazards; scanning requires interpreting signals; escorts manage interception and retreat. No role yields without input: unattended extraction cuts out on heat instead of running slowly. Beginners may choose combat or exploration instead of support work. **[S2]** Group contracts divide proceeds by agreement rather than damage alone.

An attentive beginner should outperform an unattended secondary account. This does not eliminate multiboxing: test whether one person operating several accounts outperforms a cooperating crew without meaningful trade-offs. Stage 1 runs this test with a veteran flying solo, with a newcomer, and with two clients.

## 4. Discoveries provide scarce access

Stage 1 uses NPC discovery contracts. The contract supplies the site and the player never handles keys. The site is a server record with its own seed and yield, so that keys can later attach to the same record. Repeat player demand must be demonstrated before relying on a discovery marketplace.

**[S2]** A successful scan creates a server-recorded site and a fixed, small supply of transferable **access keys**. Coordinates may be shared, but entry consumes a valid key bound to that site's signature. One key admits a declared expedition group up to its size limit. Each account occupies a roster slot; reconnection restores admission without creating another key.

**[S2]** Keys resolve to the same site record and finite resource pool, never independent copies of its rewards. Listings display expiry, remaining admissions, existing admissions, and current depletion. Explorers can use keys, sell individual admissions, or sell the complete remaining bundle. Exclusivity is promised only when no other admissions exist.

**[S2]** This prevents unauthorized copying, not resale, betrayal, or competition between authorized crews. Cooperative and contested sites have clearly disclosed rules.

## 5. Bounded combat and simulation

Outpost management, exploration, and combat share one orthographic tactical view that rests at 33° of elevation. Ships move and fight on a single gameplay plane under direct player control. Apparent height is visual presentation, not a separate targeting or movement layer. Projectiles, targeting, and module cooldowns must remain readable at the angled view.

**Camera.** In flight the view rests on the player's ship. A player may orbit it, in bearing and elevation, for as long as the look control is held; on release it springs back to the resting view. At the outpost the view stays where it is turned, so facilities may sit on every side of the station. Zoom is continuous and stays where it is left. Selecting a ship or a structure centres the view on it, so that the orbit turns around that object; any steering input returns the view to the player's own ship, so a ship is never flown while it is off screen. A reset restores the resting direction and zoom. The backdrop is a star field at infinity: it turns with the view and ignores zoom. The readability greybox at its default settings is the reference for this behaviour, and the Visual Style Guide is not yet aligned with it.

**Control.** Ships are steered directly, by thrust and turn, with client-side prediction. Weapons fire at a locked target and the server resolves hits; Stage 1 has no manually aimed shots. Selecting a hostile ship locks it: one selection serves targeting and the camera, and a control cycles to the nearest hostile. Clients send intents. The server simulates and is authoritative for movement, hits, inventory, and access. The initial latency target is full playability at 150 ms round trip and usable play at 250 ms; both are test parameters. The game requires a browser with WebTransport and a network that carries UDP; there is no fallback transport.

**Input.** Stage 1 is built and playtested with mouse and keyboard on desktop. Touch is in scope and is built after the Stage 1 gates, for tablets in landscape first; phones follow once that layout is proven. Because there is no fallback transport, a tablet needs iPadOS 26.4 or later. Gamepad support is not planned. So that touch does not force a redesign, three rules apply from Stage 1: no action is available only through hover or a right click; anything a player must select is at least a finger-sized target, provisionally 44 CSS pixels, whatever its drawn size; and every input reaches the game through one mapping layer that produces the same intents on every device. When the touch build is made, the readability measures of §9 are repeated with touch input. **Open:** the stage at which the touch build ships.

**Readability.** All combat ships render at one height. No grid, range rings or markers are drawn on the plane by default. Ships and interactable objects always show an allegiance-specific silhouette through any structure that hides them; structures do not fade. Allegiance and state are never carried by colour alone. Engagement ranges fit within the zoom at which a ship remains comfortably selectable. The resting elevation and these defaults are the owner's choice from the greybox; the Stage 1 readability gate (§9) still has to confirm them with testers who have not seen it.

The first build supports 1–4 players against NPC threats, with provisional NPC and projectile caps published in the Stage 1 specification (§10). **[S2]** Later contested sites have a **20-ship-per-side ceiling: two sides, 40 player ships total**. NPC and projectile budgets are separately capped and benchmarked. Early PvP tests begin smaller. Uncapped open-world fleet battles are outside the product commitment.

**[S2]** Admission, sides, and reinforcement slots are server-controlled. Unrelated attackers cannot occupy an existing side without roster authorization. Collusion and slot-denial require testing; a player cap alone does not solve them. Travel PvP, if introduced, must use bounded encounters rather than bypass these limits.

**[S3]** Territorial pressure comes from multiple bounded objectives. Large organizations still gain strategic breadth; the ceiling limits local concentration and simulation cost, not all political advantage. Worst-case load, latency tolerance, and combat readability are go/no-go gates before territorial play.

## 6. Ownership, loss, and offline life

**Stage 1 loss.** The home, its facilities, its stock and all unlocks are never at risk. An expedition risks what the ship carries:

| | Docked at home | Destroyed | Abandoned |
|---|---|---|---|
| Hull | Kept | Reissued at home | Returned home |
| Basic fitting | Kept | Always available | Kept |
| Upgraded modules | Kept | Lost | Kept |
| Carried cargo | Banked | Lost | Lost |
| Launch supplies | Spent | Spent | Spent |

A run is abandoned when a pilot disconnects and the instance ends before they return. A disconnected ship stays exposed for the published server timer; reconnecting within the grace window resumes in the live instance. After a server failure, each ship is restored with its fitting and cargo at its last pocket checkpoint. The launch screen shows each pilot's stake, upgraded modules and launch supplies, at NPC replacement price. A destroyed pilot returns home and may manage their outpost while the crew continues. There is no relaunch into the live instance in Stage 1.

**[S3]** Separate permanent home investment from frontier exposure:

| Asset | Rule |
|---|---|
| Personal core outpost | Secure, persistent, enhanceable through the isometric management view; cannot be conquered (Stage 1) |
| Frontier deployment | Optional forward services and claim; position can be captured |
| Protected storage | Capacity-limited, explicitly marked; recoverable after eviction |
| Operational storage | Local output, cargo, fuel, and staged fleet supplies; exposed |
| Deployed structure | Returns as a dismantled kit; installation materials and relocation costs are lost |

**[S3]** Attacks take position and operational stock. Protected assets return after a recovery delay and fee. Frontier-produced or imported cargo must be physically extracted before qualifying for protection. Recovery must not become a cheap transport service. The interface shows storage categories and maximum exposure before deployment.

**[S3]** Claims have published attack windows and advance notice, but owner attendance is optional. Prepaid NPC defense contracts provide baseline resistance, with a capped reserve response growing during continuous absence. Strength is bounded by contract tier. Absence is measured across authorized claim operators; logging out during an announced attack gives no increase. Ownership transfers cannot reset these restrictions.

**[S3]** Defenders can lose. Casual owners buy resistance and time, not immunity. An exhausted contract has a visible grace period before returning to minimum defense. Production pauses without supplies. Exact windows, reserves, and fees are test parameters; credible conflict necessarily leaves frontier owners exposed to involuntary loss.

The secure home never decays because its owner takes a break.

**Logout:** docking at home is safe. **[S2]** Expeditions provide safe pockets where a noncombat countdown suspends the ship and cargo. Login resumes the journey; it never delivers loot home. During combat, the ship remains exposed for the published server timer, including disconnects.

**[S2]** Suspended runs retain a bounded checkpoint. After site expiry, a returning pilot resumes in a designated entry pocket on the same side of the return route, with cargo but no remaining site yield. This must not bypass transport hazards. Re-entry cannot occur directly inside active combat. Checkpoint retention and logout timers require testing.

Core activities offer sustainable low-risk progression, repairs, and mission setbacks; the Stage 1 expedition is a core activity under the loss rules above. **[S3]** Frontier missions expose ships and cargo. Limited-loss contested missions may bridge these experiences without creating a third permanent population pool at launch.

## 7. Economy and recovery without constant war

**Stage 1 economy.** Extraction, salvage, NPC contract payouts and NPC buy orders are the sources. Facility construction and reconfiguration, a flat supply cost per launch, replacement of lost modules and the NPC buy/sell spread are the sinks. NPC prices are fixed and posted. Each haul offers a choice: sell raw, process at the outpost and sell, or keep for a facility. A completed lowest-tier run in the basic fitting is net positive after the launch cost. No recipe may turn inputs bought from NPCs into outputs that NPCs buy for more. Stage 1 does not evidence a steady-state economy: once the last facility is bought, the material sink is thin.

Basic materials remain inputs to advanced production. **[S2]** Manufacturing, fuel, module wear and replacement, facility supplies, and frontier deployment consume materials even during PvE. Wear accrues through use, not absence, and is visible before departure. It cannot erase permanent facility unlocks. Wear and fuel exist in the Stage 1 data model but are switched off.

**[S2]** Currency sinks include service fees, market fees, defense contracts, and asset recovery. NPC rewards and buy orders create currency. Track these separately: destroying a ship removes goods, not credits.

**NPC market makers are permanent infrastructure.** Clearly labeled standing offers guarantee basic supplies and baseline buyers for supported goods. **[S2]** Wide buy/sell spreads leave room for player competition; volumes and price adjustments are bounded. NPCs do not guarantee profitable resale of every manufactured item. Test crafting arbitrage, hauling exploits, and suppression of player trade.

Recovery time includes earning, purchasing, fitting, and returning to useful play. The basic fitting is always available and cannot be sold. The initial target is replacement of a lost upgraded fitting within 15 minutes of wall-clock play, measured from the loss, flying the lowest threat tier in the basic fitting. Advanced ships carry greater explicitly chosen stakes. Saved fittings prevent repeated defeats from making participation impossible.

Instrument every currency and material source, sink, transfer, and stock from the first build. Every change to stock goes through one ledger transfer that names its source, sink and reason. Stage 1 tracks value lost per defeat, net value per session-hour by experience, launch costs as a share of gross, the sell/process/keep choice, and per-head reward by crew size. **[S2]** Track wealth concentration, inventories, wear burden, NPC trade share, player order fill times, and recovery time by experience level.

## 8. Population and service viability

This section applies from **[S2]** unless stated.

Start shared play with one compact region and one pooled market order book. Delivery costs can preserve logistics without splitting price discovery across empty markets. PvE and NPC contracts remain available without human opponents or buyers; PvP queues never block essential progression.

Use **150 simultaneous players per shared region as an initial density hypothesis**, not a proven requirement. Measure peak/off-peak population, encounters, market activity, and queues. Open additional regions only when existing ones sustain density and service quality. Consolidate underused frontier sectors with notice; preserve homes, crew access, and ownership records.

During consolidation, structures and already protected inventory relocate safely. Exposed cargo still requires extraction, with advance notice and a clear deadline. Administrative relocation must not become an exploitable logistics shortcut. If demand cannot sustain density, retain the compact hub and session expeditions instead of expanding empty territory.

Plan an exportable authoritative server package, versioned world saves, and configurable NPC economics for private/community hosting. Community worlds have separate economies and characters; official competitive inventories cannot import untrusted saves. Test hosting and restoration before promising continuity after official service ends. Stage 1 saves are versioned and carry a world identifier from the first build (§10).

Public expansion requires funded operations, moderation, support, anti-cheat, backups, and incident recovery. Engagement caps bound simulation, not all service costs. A business model is a release gate; paid power or loss insurance must not undermine progression and risk rules.

## 9. Delivery gates and expert challenges

| Stage | Build | Gate before expansion |
|---|---|---|
| 1: Prove the signature | Isometric outpost with visible facility upgrades on fixed sockets; one authored expedition site with several pockets and seeded variation, for 1–4 players; one hull with three rigs; extraction, combat, NPC contracts and fixed-price trading, loss and recovery | Outpost enhancements change expedition decisions; angled-view targeting stays readable; players voluntarily repeat after loss; travel and recovery meet targets |
| 2: Prove shared value | Compact shared hub, contracts and trade, scarce access keys, small bounded PvP tests | Mixed-experience crews choose to regroup; keys attract repeat buyers; sparse-population economy remains usable |
| 3: Prove frontier ownership | Deployable outposts, NPC defense, capture/recovery, tested 20v20 ceiling | Offline loss is consequential but recoverable; no cheap asset evacuation; acceptable latency and operating cost |
| 4: Consider MMO scale | Adaptive regions, broader politics, live operations, hosting distribution | Sustainable density, retention, funding, security, and moderation capacity |

Stage 1 requires persistent home/account saves and authoritative cooperative sessions, not a continuously simulated galaxy, player market, or territorial server. Players sign in with a passkey. Stage 1 accounts and saves may be wiped between builds; no migration is owed to testers. Later gates may stop expansion without invalidating the smaller game.

| In Stage 1 | Not in Stage 1 |
|---|---|
| Starter outpost, fixed sockets, about three facilities with two tiers each | Free placement, frontier deployments, claims |
| One site with several pockets, seeded variation, two or three threat tiers | Additional sites, a site map, player-discovered sites, access keys |
| One hull, three rigs; progression through facilities | Further hulls, pilot progression |
| Crews by invite code, shared access, visible payout split | Shared hub, matchmaking, visiting crews, player-to-player transfers |
| NPC contracts, fixed posted prices, flat launch cost | Player market, dynamic prices, wear, fuel |
| Loss of cargo and upgraded modules; reconnect within a grace window | Cross-session suspend and checkpoints, PvP, territory |
| Mouse and keyboard on desktop | Touch controls and tablet layout; gamepad is not planned at any stage |

The Stage 1 expansion (§11) lifts four of these limits after plan M7: a second pilot as an NPC wingman, a third socket and further facilities, further hulls, and further sites with a site map at preparation; it also rebuilds the station around an internal hangar in place of the berths, seen as an exterior with facility tells and an interior with the modules and ships. Everything else in the right-hand column stays outside Stage 1.

**Stage 1 gate measures.** Playtests use about eight invited testers who have not seen the game, on machines the owner provides. The thresholds below are provisional starting figures from the Revision 3 review and are confirmed before the first measurement. With no testers available, the owner first measures Stage 1 alone (plan M7): each measure is met, failed or unproven, with its basis stated, and a measure that needs players other than the owner is unproven, never met. Gates 1 and 3 cannot be met that way. The owner may begin Stage 2 with them unproven, accepting the risk that the signature does not hold for other players; they stay unproven until other players are measured.

1. *Outpost enhancements change expedition decisions.* Among players with the same unlocks, active layouts differ; on the same seed, layout predicts pocket and rig choice; reconfigurations per ten runs are neither zero nor one per run. Testers shown before-and-after overview images can say what changed and what it lets them do. Provisionally, at least half of testers reconfigure at least once in three sessions.
2. *Angled-view targeting stays readable.* In greybox scenes that include the real station silhouette: time to select a named hostile, wrong-target rate, and judgement of heading and of which target is nearer, repeated in greyscale and under simulated colour-vision deficiency. Provisionally: a median of at most 1 second to select, at most 5% selection errors, heading judged within 22.5° at least 90% of the time, and the nearer target chosen at least 85% of the time.
3. *Players voluntarily repeat after loss.* Unprompted next-day return after a session that ended in defeat, compared with one that ended in success; the stake chosen on the launch after a defeat; the spread of choices across players on the same seed at run 10 compared with run 3. Provisionally, at least 60% of defeats are followed by another launch in the same or the next session without prompting.
4. *Travel and recovery meet targets.* Undock to first pocket plus last pocket to dock, as a share of session time; minutes from loss to an equivalent fitting; time from launch to first decision. The targets are those of §2 and §7: at most 20% of session time in travel, 15 minutes to recover, and a first decision within about two minutes.

Stage 1 also gathers early evidence for the Stage 2 crew gate, without making it a Stage 1 gate: paired playtests of a veteran and a first-session newcomer, comparing the veteran's take per hour solo, with the newcomer and with two clients, and recording whether both choose another joint run.

**What Stage 1 does not show.** It evidences the link between outpost and expedition, repeat play over roughly ten runs, and whether a second pilot helps. It does not evidence long-term retention, an open-world feel, mutual gain between newcomers and veterans, or a player economy. Those remain later gates.

**Expert challenges:** Does visibly enhancing and specializing the outpost sell the game? Do upgrades change expedition choices rather than only production rates? Is the freedom to choose pockets and to leave enough to feel free without a world map? Is isometric combat readable around large structures and overlapping ships? Is active extraction engaging rather than repetitive labor? Does a newcomer add anything a veteran's second client would not? **[S2]** Are access keys understandable and resistant to admission exploits? **[S3]** Do NPC defenders encourage absence or alternate ownership accounts? Can intentional eviction exploit recovery? Can 40-ship battles meet readability and network budgets? **[S4]** Does measured retention support the population assumption?

These questions determine product viability. Resolve them through prototypes and measured tests before committing to MMO production, rather than deferring them as balance tuning.

## 10. Stage 1 specification

The sections above state rules; this section gives values. **Every number here is a placeholder and a tunable test parameter**, chosen so that the rules above hold together, not because it has been played. The values are sized for a content horizon of about five hours: roughly ten 30-minute runs to buy three facilities at two tiers each. The checks at the end of this section should travel with the data as an automated test.

The data model is fixed now so that later stages do not need a rewrite. Facilities are data with capability tags. Expedition options are keyed to capability tags, not to facility identifiers. A site is a template plus a seed plus parameters. Saves are versioned, carry a world identifier, and store unlocks separately from the active layout. Items have identities, so that trade and keys can refer to them later. Ships and fittings are instances with a wear field.

A client-only readability greybox comes before server work and before further station art. Construction sockets and facility modules are authored through the scene generator in Stage 1, and from plan M8 modelled by hand through the art pipeline (`Design/Art/`); primitive stand-ins are not the plan.

**Materials and currency (placeholders).** One currency, credits (cr). Three raw materials come from the site and two goods are processed at the outpost.

| Item | Source | Used for |
|---|---|---|
| Ore | Ore field | Tier 1 facilities; alloy |
| Salvage | Derelict, nest core | Components |
| Volatiles | Volatile veins | Sale |
| Alloy | 2 ore | Tier 2 facilities; reinstallation |
| Components | 2 salvage | Tier 2 facilities |

**Outpost and facilities (placeholders).** The starter outpost has a fixed core (berth, cargo bay, a processor that takes 4 minutes over a batch) and two construction sockets. Socket A, beside the cargo bay, is open and empty at the start. Socket B, beside the berth, opens when the first facility is installed. Any facility fits either socket. The power cap is 5; a tier 1 facility draws 2 and a tier 2 facility draws 3. Three facilities cannot all be installed, two cannot both run at tier 2, and a tier 2 facility may be run at tier 1 draw with tier 1 capabilities. An unpowered facility grants nothing. Construction is instant.

| Facility | Tier | Cost | Opens on the expedition | Home side |
|---|---|---|---|---|
| Sensor array | 1 | 150 cr + 10 ore | Derelict pocket; scout rig | None |
| | 2 | 550 cr + 12 alloy + 5 components | Deep-scan array module | None |
| Refinery | 1 | 150 cr + 10 ore | Volatile veins pocket; extraction rig; cargo frame module | Processor takes 2 minutes over a batch |
| | 2 | 550 cr + 12 alloy + 5 components | Heat sink module | Processor takes 1 minute over a batch |
| Fabrication bay | 1 | 150 cr + 10 ore | Nest core pocket; escort rig | None |
| | 2 | 550 cr + 12 alloy + 5 components | Point-defence module | None |

Adjacency: a refinery in socket A processes batches in 20% less time; a fabrication bay in socket B cuts rig and module replacement prices by 20%. The purchase price includes the first installation. Installing a facility again after removing it costs 40 cr and 6 alloy; removal is free. Capabilities come from the active, powered layout, not from the list of unlocks.

**Ship, rigs and modules (placeholders).** One hull carries one rig and up to two modules. The first rig comes with its facility. A rig or module can be fitted only while its facility is installed and powered at the required tier.

| Fitting | From | Cargo | Effect | NPC replacement |
|---|---|---|---|---|
| Basic fitting | Starter; always available; cannot be sold or lost | 20 | Scans, extracts and fights at basic rates | None |
| Scout rig | Sensor array 1 | 16 | Fast scan; salvage at 1.6 per minute; 20% faster | 60 cr |
| Extraction rig | Refinery 1 | 32 | Ore at 3.0 and volatiles at 1.4 per minute | 60 cr |
| Escort rig | Fabrication bay 1 | 12 | Double weapon output; shield | 60 cr |
| Cargo frame | Refinery 1 | +6 | None | 30 cr |
| Deep-scan array | Sensor array 2 | No change | Reveals the hidden seed values from the approach leg | 90 cr |
| Heat sink | Refinery 2 | No change | 25% more extraction before cut-out | 90 cr |
| Point defence | Fabrication bay 2 | No change | Intercepts projectiles near the ship | 90 cr |

**The site (placeholders).** Four pockets are visible from the first run; three are locked until some crew member's outpost opens them. Yields are per run at tier I for a solo pilot. The basic fitting can work any pocket the crew can enter.

| Pocket | Needs | Yield | Basic rate | Hazard |
|---|---|---|---|---|
| Ore field | Nothing | 40 ore | 2.0 per minute | Drones gather on extracting ships |
| Derelict | Sensor array, which locates its signal | 18 salvage | 1.2 per minute | Tight interior; raiders arrive from alert 2 |
| Volatile veins | Refinery | 16 volatiles | 0.8 per minute | Heat builds twice as fast; the seed hazard is at full strength |
| Nest core | Fabrication bay | 120 cr bounty + 6 salvage | Cleared in about 14 minutes; 8 with an escort rig | Wardens hold the core |

Extraction builds heat. At full heat the extractor cuts out for 20 seconds; venting is a manual action that pauses extraction for 5 seconds. The contract pays when a ship docks with at least 10 units of site cargo.

| Seed parameter | Values | Known |
|---|---|---|
| Hazard type | Thermal flare (heat builds 30% faster), debris drift (collision damage between pockets), ion fog (scan and lock range down 40%) | Shown in preparation |
| Dominant threat | Drones, raiders or balanced | Shown in preparation |
| Rich pocket | One pocket has 1.5 times its yield and rate | Revealed by scanning in the site |
| Wave composition and timing | Drawn from the threat mix | Revealed by scanning in the site |
| Relay positions | Two different pockets | Revealed by scanning in the site |

| Threat tier | Contract payout | Yield pools | Opening threat | Alert rises every | NPC speed | NPC damage |
|---|---|---|---|---|---|---|
| I | 100 cr | ×1.0 | 3 NPCs | 4 minutes | ×0.5 | ×0.25 |
| II | 160 cr | ×1.25 | 5 NPCs | 3 minutes | ×1.0 | ×1.0 |
| III | 240 cr | ×1.5 | 8 NPCs | 2 minutes | ×1.0 | ×1.0 |

Alert also rises by one each time any extractor cuts out on heat. Each alert step adds a wave of one NPC plus one per tier number. On the return leg, one raider intercepts for each full third of cargo capacity carried that no earlier interception has answered: leaving the site empty does not spend the ambush, and the same cargo is not intercepted twice. At tier I, NPCs fly at half speed, so the starter hull leaves them behind without its cruise drive, and hit a quarter as hard, so a first run can be survived and escaped; tiers II and III field NPCs at full strength. Tier I is completable in the basic fitting. Tiers open with facilities: tier II when a player first owns a tier 2 facility, tier III when they own a second. They follow unlocks, not the active layout. The host may choose any tier that a crew member has opened.

The optional two-place objective is a relay link: two relays in different pockets must each be held by a ship for the same 30 seconds. It adds 50% to the contract payout.

**Threats and crew scaling (placeholders).** Provisional caps are 24 NPCs and 64 projectiles alive in one instance; these are the benchmark targets of §5. When the NPC cap binds, wardens are placed first, so the nest is always fully guarded; the opening threat takes the room left, and NPCs of a wave that do not fit are dropped.

| NPC | Behaviour |
|---|---|
| Drone | Swarms the nearest extracting ship; fragile, short range |
| Raider | Hunts the ship carrying the most cargo; intercepts on the return leg |
| Warden | Guards the nest core; slow, heavy, long range; never leaves the core |

| Pilots | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Threat (opening and wave size) | ×1.0 | ×1.5 | ×1.9 | ×2.2 |
| Yield pools | ×1.0 | ×1.8 | ×2.5 | ×3.1 |
| Contract payout, whole expedition | ×1.0 | ×1.6 | ×2.2 | ×2.8 |

Threat never scales with experience or fittings. The contract payout, the nest bounty and the relay bonus form one pot, split equally unless the host changes the split before launch. Cargo belongs to the ship that docks with it.

**Prices, recipes and costs (placeholders).** Prices are fixed and posted. The starting balance is 100 cr and each launch costs each pilot 40 cr in supplies. A pilot short of 40 cr who holds goods, in stock or queued for processing, sells first; one whose credits and goods together fall short launches for the credits they have, so nobody is stranded. Supplies are spent by a contract that pays: a pilot who docks from an expedition whose contract was not paid still has them aboard, and their next launch costs nothing.

| Good | NPC buys at | NPC sells at |
|---|---|---|
| Ore | 10 | 16 |
| Salvage | 18 | 28 |
| Volatiles | 22 | 36 |
| Alloy | 26 | 40 |
| Components | 46 | 70 |

Recipes: 2 ore make 1 alloy; 2 salvage make 1 components. Batches are loaded at the dock and the processor works them one at a time, in the order loaded, each taking the time the outpost's layout gives it. It works by the server's clock: while the pilot is home, flying or signed out, and whatever the outcome of a run. A batch not yet finished can be taken back for its inputs; the one in hand loses the work done on it. When the layout changes the processor's pace, the batch in hand stays as far along as it was. Rigs, modules and facilities cannot be sold.

**View (placeholders).** The view starts 40 metres wide and zooms between 12 and 140 metres. While the look control is held the elevation can range from 8° to 88°. The resting elevation of 33° and the orthographic projection are decisions, not placeholders.

**Timing (placeholders).**

| Parameter | Value |
|---|---|
| Simulation tick and state snapshots | 20 per second; clients interpolate 100 ms behind |
| Preparation | 4 minutes |
| Approach leg | 3 minutes; first decision within 2 minutes of launch |
| Objective | 16 minutes |
| Return leg | 3 minutes |
| Cruise drive | 3 times speed and acceleration, outside the site |
| Jump drive | Spools for 6 seconds, once the ship is 30 m clear of the dock; arrives 40 m outside a pocket's edge, 40 m from the dock when empty, or beyond the interceptors' reach (still outside the site) when carrying cargo |
| Unloading and decisions | 4 minutes |
| Instance limit | 45 minutes, with a warning at 40 |
| Disconnect exposure timer | 30 seconds in combat, 10 seconds otherwise; the ship is then held inert and cannot be targeted |
| Reconnect grace window | 5 minutes, or until the instance ends if sooner |
| Pocket checkpoint | On entering a pocket and every 60 seconds |

The instance ends when every connected pilot has docked or been destroyed, or at the limit.

**Loss and recovery (placeholders).** The launch screen prices the stake from the tables above: an extraction rig, heat sink and cargo frame show 180 cr of modules and 40 cr of supplies. The 15-minute recovery of §7 is budgeted as 1.5 minutes of preparation, 3 of approach, 6.5 of extraction in the ore field, 3 of return and 1 to buy and refit. At basic rates that is 13 ore and the tier I payout, 190 cr after supplies, against 180 cr for the most expensive fitting.

**Checks on these values.** Each holds for the numbers above.

- A completed tier I run in the basic fitting returns 20 ore and nets 260 cr after supplies.
- After the first run a new player holds 260 cr and 10 ore, enough for one tier 1 facility.
- For every good the NPC sells above the price it buys at, and each recipe's inputs bought from NPCs cost more than NPCs pay for the output (32 against 26; 56 against 46). Processing a pilot's own haul still gains (20 becomes 26; 36 becomes 46).
- Three tier 1 facilities draw 6 and two tier 2 facilities draw 6, both over the cap of 5; one tier 2 and one tier 1 draw exactly 5.
- A solo pilot with no losses buys all six tiers in nine completed runs, valuing materials at NPC buy prices. One loss or a few reinstallations makes it about ten.

## 11. Stage 1 expansion

The owner has no testers (§9), so the game must hold one player for longer than Stage 1's
content horizon of about ten runs. Once plan M7 has measured Stage 1 as §10 specifies it, the
expansion lifts four Stage 1 limits: the second pilot, the outpost's size, the single hull and
the single site. It also rebuilds the station around an internal hangar, in place of the berths, and splits it into an exterior and an interior view. It adds no Stage 2 system: no shared hub, player market, access keys or PvP.
Progression still comes only from facilities (§3): every new option is opened by a facility,
so the expansion widens the outpost's signature instead of working around it. Every value below
is a placeholder and a tunable test parameter, as in §10, sized for about fifteen hours of play,
roughly thirty runs.

**The wingman.** A pilot may launch with one wingman: an NPC pilot from the outpost's crew
quarters, flying the starter hull. A wingman is a pilot for the purposes of threat and yield
scaling (§10's crew table), but the contract payout is not scaled for it and it takes no share
of the pot; its supplies cost the same launch cost as a pilot's. It follows simple orders and
sees only what a pilot could see: it never scans, chooses pockets or knows a hidden seed value.
It is weaker than an attentive human pilot by design, so §3's newcomer still adds what a
wingman cannot, and a wingman is no evidence for the Stage 2 crew gate.

| Order | The wingman | Needs |
|---|---|---|
| Follow (default) | Stays near the pilot and fights whatever attacks either ship | Crew quarters 1 |
| Hold | Holds the place the pilot marks, such as a relay, and fights there | Crew quarters 1 |
| Return | Flies home and docks | Crew quarters 1 |
| Extract | Works the deposit the pilot marks, venting on its own; its cargo banks to the pilot when it docks | Crew quarters 2 |

A destroyed wingman loses its cargo and fitting, and is ready again for the next launch. Ships in
one instance, pilots and wingmen together, never exceed four, so a crew of four has no wingmen
and §5's caps and snapshot budget stand.

**The station: exterior and interior.** The station is seen two ways, in the one tactical
camera (§5). The **exterior** is the station as it stands at the dock in every run and at home:
two to three times larger than today's, with one hangar mouth and no berths, apron, sockets or
parked ships. It still changes as the outpost grows: each facility shows a small **tell** at its
own fixed hardpoint on the hull, which grows with the facility's tier, and the reactor upgrade
has one too, so the outside evolves even though the modules are inside. Selecting the station at
home opens the **interior**: a cutaway of the same station, seen through the same orthographic
view, orbit and zoom, showing the construction sockets with their facility modules and states
(empty, building, powered, unpowered, producing, blocked; §2), the processor and cargo bay, and
the hangar deck with every ship the pilot owns. Building, upgrading and reconfiguring happen in
the interior. Gate 1's before-and-after images show both views.

A ship launches by flying out of the mouth, and crew ships leave one after another; it docks by
flying into the mouth, and docking is still flown (§2). Undocking, docking and a jump home are
measured from the mouth, and a jump's arrival is always outside the station. The mouth is the
only fit a hull must pass outside: its clear opening is wider and taller than the largest hull
with the style guide's clearance on every side; inside, every hull has a parking place on the
hangar deck. Socket B moves beside the hangar deck and keeps its adjacency.

**A deeper outpost.** A reactor upgrade to the outpost's core (600 cr + 15 alloy, once) opens a
third construction socket, C, beside the processor in the interior, and raises the power cap from 5 to 8. The
three Stage 1 facilities gain a third tier, and three new facilities open the wingman, hulls and
sites, so six facilities compete for three sockets. A tier 3 facility draws 4.

| Facility | Tier | Cost | Opens |
|---|---|---|---|
| Sensor array | 3 | 1,200 cr + 20 alloy + 12 components | The rich pocket and the wave composition shown in preparation |
| Refinery | 3 | 1,200 cr + 20 alloy + 12 components | Fuel cells: 2 volatiles make 1 fuel cell |
| Fabrication bay | 3 | 1,200 cr + 20 alloy + 12 components | Armour plating module |
| Crew quarters | 1 | 150 cr + 10 ore | A wingman in the basic fitting |
| | 2 | 550 cr + 12 alloy + 5 components | The extract order; the wingman may fly a rig from the outpost's stock, at stake |
| Shipyard | 1 | 300 cr + 10 alloy | Hauler hull; afterburner module |
| | 2 | 700 cr + 15 alloy + 6 fuel cells | Gunship hull |
| Navigation array | 1 | 300 cr + 10 alloy | Second site on the site map |
| | 2 | 700 cr + 15 alloy + 6 fuel cells | Third site |

Adjacency for socket C: a navigation array there shows each site's hazard and dominant threat on
the site map before a contract is taken. Fuel cells are a sink as well as a good: the NPC buys
them at 60 and sells them at 90.

**Ships and fittings.** The shipyard's hulls are chosen at launch, like a rig. A destroyed hauler
or gunship is lost like an upgraded module (§6): the pilot comes home in the starter hull and
replaces the lost one at its NPC price.

| Hull | From | Cargo | Handling | Combat | Slots | NPC replacement |
|---|---|---|---|---|---|---|
| Frigate (starter) | Always | ×1.0 | As §10 | As §10 | 1 rig, 2 modules | Reissued free |
| Hauler | Shipyard 1 | ×1.75 | 25% slower; turns slower; larger | Weapons ×0.75 | 1 rig, 1 module | 180 cr |
| Gunship | Shipyard 2 | ×0.6 | As the frigate | Weapons ×1.5; shield | 1 rig, 2 modules | 260 cr |

| Module | From | Effect | NPC replacement |
|---|---|---|---|
| Afterburner | Shipyard 1 | The cruise drive's speed inside the site for 10 seconds, every 60 seconds | 90 cr |
| Armour plating | Fabrication bay 3 | 50% more hull; 10% slower | 90 cr |

**Sites and the site map.** At preparation, a site map lists the sites the outpost's navigation
array opens; each has its own contract at the chosen threat tier. In flight the system map still
shows only the site being flown. Each new site is a template with its own pockets, one new
hazard and one new NPC, and its pockets are keyed to capability tags like the first site's.

| Site | Opened by | Character | New hazard | New NPC | Payout |
|---|---|---|---|---|---|
| The first site (§10) | Always | Balanced | §10's | None | ×1.0 |
| Shattered moon | Navigation array 1 | Dense debris everywhere; salvage-rich; tight pockets | Debris collides everywhere, not only between pockets | Sentinel: a stationary turret that guards a pocket; long range, heavy hull, never moves | ×1.2 |
| Ice ring | Navigation array 2 | Wide and open; long legs; volatiles-rich | Ion storm: lock range halves for 20 seconds every 2 minutes | Stalker: fast, stays beyond lock range until it strikes; a scan reveals it | ×1.4 |

**Checks on these values.** Each is to hold for the numbers above, and travels with the data as
an automated test, like §10's.

- Every hull passes through the hangar mouth flying straight in, with the style guide's
  clearance on every side, and has a parking place on the interior's hangar deck.
- Pilots and wingmen together never exceed four ships in an instance, so §10's snapshot test of
  4 ships and 24 NPCs still bounds every instance.
- With six facilities, three sockets and a power cap of 8, no layout runs everything: a tier 3,
  a tier 2 and a tier 1 facility draw 9.
- Fuel cells are no arbitrage: 2 volatiles bought from the NPC cost 72, more than the 60 the NPC
  pays for a fuel cell.
- A solo pilot buys everything in §10 and here in about thirty runs.
