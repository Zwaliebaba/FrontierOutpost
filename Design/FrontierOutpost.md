# Frontier Outpost 

**Game design · Revision 3 · 9 November 2026 · Proposal for expert review**

## 1. Identity and scope

**Establish and enhance your outpost. Equip ships for dangerous expeditions, bring discoveries home, and expand the facilities that determine what you can build and where you can go.**

Frontier Outpost is an isometric space game. Its signature is a visibly evolving outpost whose layout, upgrades, and specialization determine expedition capabilities. A trailer should show ships returning with salvage, a new facility transforming the outpost, and the launch of a newly possible mission. Exploration, combat, and trade support this experience.

Players choose outpost development, exploration, fighting, or a mixture each session. Trade allows specialization; nobody must personally gather, manufacture, and fight to progress. Solo players and crews of 1–4 are the initial audience.

**Build an expedition game with persistent homes first.** Shared regions are a gated expansion. A full MMO depends on demonstrated population density, technical capacity, and sustainable operations. If those gates fail, the smaller game remains a complete product. Familiar space-economy features alone do not differentiate it from EVE Online.

## 2. The home and the expedition

**Configure home → prepare → explore, extract, or fight → return → trade and develop → choose a new objective.**

| Activity | Meaningful decisions | Contribution |
|---|---|---|
| Outpost development | Facility placement and upgrades, power allocation, production, storage, contracts | Expedition preparation, processing, services |
| Exploration | Route, scanning, hazard interpretation, access-key use, when to leave | Access to finite opportunities, salvage, resources |
| Combat | Piloting, positioning, targeting, module timing, retreat | Protects extraction and transport, contests opportunities |

Players manage the outpost from an isometric overview, selecting facilities to build, upgrade, specialize, or reconfigure. Authored external modules attach to defined construction sockets. Limited power, attachment points, and functional adjacency create choices. A sensor facility enables particular scans; a cargo/refinery connection improves processing; allocating a bay to repair reduces fabrication capacity. Expansion adds options without eliminating constraints.

Enhancements change the outpost's silhouette, visible equipment, and operational capabilities. Upgrade previews show resource costs, power demand, and the capability gained. Specialization branches offer alternatives rather than an endless ladder of larger numbers. Facility selection opens concise management panels; there is no avatar movement or explorable interior. Docked ships, visiting crews, and visible production activity give the home social presence. Everyone receives a starter outpost; deep customization is optional.

**Session budget:** target 30–60 minutes, with ordinary outbound and return travel together at most 20% of session time. A 30-minute example allocates 4 minutes to preparation, 6 to travel, 16 to the objective, and 4 to unloading and decisions. Longer expeditions are explicit commitments. Frontier staging reduces travel while exposing supplies and position.

## 3. Progression and useful newcomers

Players develop a hangar of specialized ships rather than one indefinitely superior vessel. Initial roles are scout/frigate, extraction ship, and escort. Basic solo fittings combine functions less efficiently, so essential activities never require a full crew.

Small hulls have intrinsic capabilities that skill progression cannot buy for large hulls: low signatures, narrow wreck access, and entry into mass-limited hazard pockets. Larger ships gain capacity or endurance but retain weaknesses. Veterans still have reasons to fly inexpensive ships.

Pilot progression primarily unlocks fittings and specialization. Numerical proficiency bonuses have explicit ceilings and cannot replace role choice or piloting. New players should reach one useful role in their first session. Veterans pursue difficult sites, outpost configurations, production businesses, and collective objectives.

Support is active play. Extraction balances heat, yield, and changing hazards; scanning requires interpreting signals; escorts manage interception and retreat. Group contracts divide proceeds by agreement rather than damage alone. Beginners may choose combat or exploration instead of support work.

An attentive beginner should outperform an unattended secondary account. This does not eliminate multiboxing: test whether one person operating several accounts outperforms a cooperating crew without meaningful trade-offs.

## 4. Discoveries provide scarce access

A successful scan creates a server-recorded site and a fixed, small supply of transferable **access keys**. Coordinates may be shared, but entry consumes a valid key bound to that site's signature. One key admits a declared expedition group up to its size limit. Each account occupies a roster slot; reconnection restores admission without creating another key.

Keys resolve to the same site record and finite resource pool, never independent copies of its rewards. Listings display expiry, remaining admissions, existing admissions, and current depletion. Explorers can use keys, sell individual admissions, or sell the complete remaining bundle. Exclusivity is promised only when no other admissions exist.

This prevents unauthorized copying, not resale, betrayal, or competition between authorized crews. Cooperative and contested sites have clearly disclosed rules. The first prototype uses NPC discovery contracts; repeat player demand must be demonstrated before relying on a discovery marketplace.

## 5. Bounded combat and simulation

Use a consistent fixed-angle isometric camera for outpost management, exploration, and combat, with zoom and pan appropriate to each activity. Ships move and fight on a single gameplay plane under direct player control. Apparent height is visual presentation, not a separate targeting or movement layer. Projectiles, targeting, and module cooldowns must remain readable at the angled view. Foreground structures fade or provide outlines when they obscure ships or interactable objects. The server authoritatively validates movement, hits, inventory, and access.

The first build supports 1–4 players against NPC threats. Later contested sites have a **20-ship-per-side ceiling: two sides, 40 player ships total**. NPC and projectile budgets are separately capped and benchmarked. Early PvP tests begin smaller. Uncapped open-world fleet battles are outside the product commitment.

Admission, sides, and reinforcement slots are server-controlled. Unrelated attackers cannot occupy an existing side without roster authorization. Collusion and slot-denial require testing; a player cap alone does not solve them. Travel PvP, if introduced, must use bounded encounters rather than bypass these limits.

Territorial pressure comes from multiple bounded objectives. Large organizations still gain strategic breadth; the ceiling limits local concentration and simulation cost, not all political advantage. Worst-case load, latency tolerance, and combat readability are go/no-go gates before territorial play.

## 6. Ownership, loss, and offline life

Separate permanent home investment from frontier exposure:

| Asset | Rule |
|---|---|
| Personal core outpost | Secure, persistent, enhanceable through the isometric management view; cannot be conquered |
| Frontier deployment | Optional forward services and claim; position can be captured |
| Protected storage | Capacity-limited, explicitly marked; recoverable after eviction |
| Operational storage | Local output, cargo, fuel, and staged fleet supplies; exposed |
| Deployed structure | Returns as a dismantled kit; installation materials and relocation costs are lost |

Attacks take position and operational stock. Protected assets return after a recovery delay and fee. Frontier-produced or imported cargo must be physically extracted before qualifying for protection. Recovery must not become a cheap transport service. The interface shows storage categories and maximum exposure before deployment.

Claims have published attack windows and advance notice, but owner attendance is optional. Prepaid NPC defense contracts provide baseline resistance, with a capped reserve response growing during continuous absence. Strength is bounded by contract tier. Absence is measured across authorized claim operators; logging out during an announced attack gives no increase. Ownership transfers cannot reset these restrictions.

Defenders can lose. Casual owners buy resistance and time, not immunity. An exhausted contract has a visible grace period before returning to minimum defense. Production pauses without supplies. The secure home never decays because its owner takes a break. Exact windows, reserves, and fees are test parameters; credible conflict necessarily leaves frontier owners exposed to involuntary loss.

**Logout:** docking at home is safe. Expeditions provide safe pockets where a noncombat countdown suspends the ship and cargo. Login resumes the journey; it never delivers loot home. During combat, the ship remains exposed for the published server timer, including disconnects.

Suspended runs retain a bounded checkpoint. After site expiry, a returning pilot resumes in a designated entry pocket on the same side of the return route, with cargo but no remaining site yield. This must not bypass transport hazards. Re-entry cannot occur directly inside active combat. Checkpoint retention and logout timers require testing.

Core activities offer sustainable low-risk progression, repairs, and mission setbacks. Frontier missions expose ships and cargo. Limited-loss contested missions may bridge these experiences without creating a third permanent population pool at launch.

## 7. Economy and recovery without constant war

Basic materials remain inputs to advanced production. Manufacturing, fuel, module wear and replacement, facility supplies, and frontier deployment consume materials even during PvE. Wear accrues through use, not absence, and is visible before departure. It cannot erase permanent facility unlocks.

Currency sinks include service fees, market fees, defense contracts, and asset recovery. NPC rewards and buy orders create currency. Track these separately: destroying a ship removes goods, not credits.

**NPC market makers are permanent infrastructure.** Clearly labeled standing offers guarantee basic supplies and baseline buyers for supported goods. Wide buy/sell spreads leave room for player competition; volumes and price adjustments are bounded. NPCs do not guarantee profitable resale of every manufactured item. Test crafting arbitrage, hauling exploits, and suppression of player trade.

Recovery time includes earning, purchasing, fitting, and returning to useful play. The initial target is replacement of a basic expedition fitting within 15 minutes of dependable play. Advanced ships carry greater explicitly chosen stakes. Saved fittings and protected basic reserves prevent repeated defeats from making participation impossible.

Instrument every currency and material source, sink, transfer, and stock from the first build. Track wealth concentration, inventories, wear burden, NPC trade share, player order fill times, and recovery time by experience level.

## 8. Population and service viability

Start shared play with one compact region and one pooled market order book. Delivery costs can preserve logistics without splitting price discovery across empty markets. PvE and NPC contracts remain available without human opponents or buyers; PvP queues never block essential progression.

Use **150 simultaneous players per shared region as an initial density hypothesis**, not a proven requirement. Measure peak/off-peak population, encounters, market activity, and queues. Open additional regions only when existing ones sustain density and service quality. Consolidate underused frontier sectors with notice; preserve homes, crew access, and ownership records.

During consolidation, structures and already protected inventory relocate safely. Exposed cargo still requires extraction, with advance notice and a clear deadline. Administrative relocation must not become an exploitable logistics shortcut. If demand cannot sustain density, retain the compact hub and session expeditions instead of expanding empty territory.

Plan an exportable authoritative server package, versioned world saves, and configurable NPC economics for private/community hosting. Community worlds have separate economies and characters; official competitive inventories cannot import untrusted saves. Test hosting and restoration before promising continuity after official service ends.

Public expansion requires funded operations, moderation, support, anti-cheat, backups, and incident recovery. Engagement caps bound simulation, not all service costs. A business model is a release gate; paid power or loss insurance must not undermine progression and risk rules.

## 9. Delivery gates and expert challenges

| Stage | Build | Gate before expansion |
|---|---|---|
| 1: Prove the signature | Isometric outpost with visible facility upgrades; one 1–4-player expedition; extraction, combat, NPC trading, loss and recovery | Outpost enhancements change expedition decisions; angled-view targeting stays readable; players voluntarily repeat after loss; travel and recovery meet targets |
| 2: Prove shared value | Compact shared hub, contracts and trade, scarce access keys, small bounded PvP tests | Mixed-experience crews choose to regroup; keys attract repeat buyers; sparse-population economy remains usable |
| 3: Prove frontier ownership | Deployable outposts, NPC defense, capture/recovery, tested 20v20 ceiling | Offline loss is consequential but recoverable; no cheap asset evacuation; acceptable latency and operating cost |
| 4: Consider MMO scale | Adaptive regions, broader politics, live operations, hosting distribution | Sustainable density, retention, funding, security, and moderation capacity |

Stage 1 requires persistent home/account saves and authoritative cooperative sessions, not a continuously simulated galaxy, player market, or territorial server. Later gates may stop expansion without invalidating the smaller game.

**Expert challenges:** Does visibly enhancing and specializing the outpost sell the game? Do upgrades change expedition choices rather than only production rates? Is isometric combat readable around large structures and overlapping ships? Is active extraction engaging rather than repetitive labor? Are access keys understandable and resistant to admission exploits? Do NPC defenders encourage absence or alternate ownership accounts? Can intentional eviction exploit recovery? Can 40-ship battles meet readability and network budgets? Does measured retention support the population assumption?

These questions determine product viability. Resolve them through prototypes and measured tests before committing to MMO production, rather than deferring them as balance tuning.
