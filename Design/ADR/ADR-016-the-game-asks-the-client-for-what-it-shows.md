# ADR-016: The game asks the client for what it shows

- **Status:** Accepted (owner, 2026-09-26, with the library split's Phase 2)
- **Scope:** how GameLogic reaches everything that is drawn, played or shown, now that it may not
  see NeuronClient (ADR-014). `GameLogic/Visual.h`, `GameLogic/Presentation.h`, the
  `FrontierOutpost/*Visual.cpp` files, `FrontierOutpost/ClientPresentation.cpp` and
  `FrontierOutpost/GameShaderRegistry.cpp`.
- **Detail:** `Design/LibrarySplit-plan.md` §3 and §4.1

## Context

ADR-014 layers the game over two engine libraries, and GameLogic sees only NeuronCore. The imported
game did not separate what it simulates from what it shows. Objects drew themselves (`OnDraw`, the
interior hooks, a `RenderComponent` in each), item types built their models, textures and GPU
generated shapes as they were generated, interiors owned particle systems, game events spawned
particle effects, and objects returned UI widgets. About 70 GameLogic files included NeuronClient
headers.

## Decision

1. **GameLogic declares what it asks for; the client registers what answers.**
   `GameLogic/Visual.h` holds four registries, filled by the executable's static constructors:
   - `RegisterVisual` / `CreateVisual`: an object's renderable, by kind. The eight objects that
     drew through a `RenderComponent`, and the shared models of wormholes, warp nodes and pods, and
     a System's and a Colony's look.
   - `RegisterSeededVisual` / `CreateSeededVisual`: a renderable from a seed: an asteroid's shape,
     for asteroids and a colony.
   - `RegisterItemVisual` / `CreateItemVisual`: an item type's look as it is generated, such as a
     planet's surface or a station's hull and interior. What the generator drew from the item's RNG
     for it is passed as `_seed`, so the item's other draws stay in order.
   - `RegisterDraw` / `Draw`: an object's own draw methods by kind and phase (object, interior,
     before and after the interior), for DustFlecks, Station, System and Colony.
   A factory fills a `Reference<RenderableT>&`, never returns a bare pointer, so what it makes is
   held from the moment it exists.
2. **One `Game::Presentation` holds the rest** (`GameLogic/Presentation.h`, installed by
   `FrontierOutpost/ClientPresentation.cpp`):
   - drawing an object's renderable and an interior;
   - the view's position, which a zone's asteroid fields follow;
   - each interior's particles, which it steps and makes current around the interior's update;
   - the particle effects game events set off (`BeamHit`, `ParticleFirefly`, `SmallPlume`).
3. **The bodies moved verbatim.** What a registry or the presentation now calls is the game's own
   code, moved as it was. Where the client needed to read an object or item it could not see, the
   class moved from its `.cpp` into a header, also as it was. Calls stay where the game made them,
   so a client's run draws on the shared `Rand` in the same order.
4. **A widget is written to storage the client owns.** `ObjectT::GetWidget(Player const&, void*)`
   replaces the version that returned a `Widget`, as scripted objects already did through
   `ScriptFunction::VoidCall`. The `Object_GetWidget` script binding is the client's.
5. **Shaders go with the code that uses them.** A shader NeuronClient's own code draws with stays
   in `NeuronClient/Shaders/`; the game's are in `FrontierOutpost/Shaders/`, held by
   `GameShaderRegistry.cpp`, which `ShaderRegistry_Add` joins to the one lookup by legacy name.
6. **The proof is a link.** `Tests/GameLogicTests` links NeuronCore, GameLogic and NeuronServer
   whole and NeuronClient not at all. A client symbol left in the game fails that link.

## What this forecloses

- GameLogic including a NeuronClient header, naming a GPU, window, input, audio or UI type, or
  making anything it shows. The compiler enforces the first; the test link enforces the rest.
- Visual state as a reflected field of a game object: a script cannot read a system's nebula or a
  colony's sky.

## What is left for the server (not decided here)

Without a client, the registries answer nothing, and some of that is more than looks:

- **Shapes.** Asteroids, pods, station hulls and the models LTSL scripts build for ship, turret,
  thruster and transfer-unit types come from the client, and physics takes collision meshes from
  those renderables. A server has no collision shapes until it has a way to make them, and SDF
  shapes are built on the GPU (ADR-009). The plan's item 6 (a geometry service in NeuronCore) is
  deferred to that decision, because the service is the answer to it, not a step before it.
- **Scripts.** The item-generation scripts call client script functions (`Model_Create` and the
  like), which a server does not register.
- **Randomness.** The particle effects draw on the shared `Rand`. A run without a presentation
  draws fewer numbers, so a deterministic server and client would diverge (AGENTS.md R16).
