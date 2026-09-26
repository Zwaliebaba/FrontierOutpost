# Library split: one executable, four libraries

- **Status:** Approved 2026-09-26. P0 and P1 done (ADR-014, ADR-015). P2, the game untangle, is
  next; §4.1 lists what is left.
- **Replaces:** `launch.exe` + `lt.dll` + `NeuronClient.lib`
- **Owner's answers so far:** untangle fully now; legacy files keep ADR-001's exemption per file;
  clashing files get their former folder as a prefix, and only clashing files are renamed;
  `script/` goes to `Tools/`, README to the root, LICENSE stays with the imported code, and
  UTF8/windirent go into NeuronCore.

## 1. Target

```
FrontierOutpost.exe   the client, and later the server (--serve): main(), the game's client side
 ├── GameLogic.lib    the game's rules and state: objects, items, components, AI, physics
 │    └── NeuronCore.lib
 ├── NeuronClient.lib rendering, shaders, window, input, glyphs, images, sound, the UI toolkit
 │    └── NeuronCore.lib
 ├── NeuronServer.lib the server's engine side (a placeholder today)
 │    └── NeuronCore.lib
 └── NeuronCore.lib   the engine shared by both sides: containers, math, strings, types, the
                      LTSL compiler and runtime, geometry, SDFs, OS, threads, jobs, scheduler
Tests/NeuronClientTests   unchanged
Tests/GameLogicTests      new: links NeuronCore + GameLogic + NeuronServer and nothing else
```

The edges run one way (AGENTS.md R9). NeuronClient and NeuronServer never see GameLogic, and
**GameLogic never sees NeuronClient, by include or by link.** Code that needs both the game and the
client (drawing a planet, building an item's icon, the render passes, the sound adapter that
follows the camera) is the game's client side and lives in the executable's project. Nothing else
can hold it without an upward edge.

**What proves it.** `Tests/GameLogicTests` links GameLogic without NeuronClient. If a single
render symbol is left in GameLogic or NeuronCore, that test project stops linking. Its first
test builds a Universe from a seed and ticks it, which is what a server does.

### 1.1 Directory layout

```
FrontierOutpost.slnx
FrontierOutpost/   FrontierOutpost.vcxproj(.filters), Main.cpp (from launch.cpp), Resources.rc,
                   Resource.h, the game's client files, Shaders/ (the game's shaders), LICENSE
NeuronCore/        NeuronCore.vcxproj(.filters), flat
NeuronClient/      as today, plus the legacy client files, Shaders/ (the engine's shaders)
NeuronServer/      NeuronServer.vcxproj(.filters), one placeholder source
GameLogic/         GameLogic.vcxproj(.filters), flat
Tests/NeuronClientTests/, Tests/GameLogicTests/
Tools/             assetlist.py, tloc.py, tloc, meta/ (from FrontierOutpost/script/)
README.md          the old FrontierOutpost/README.md, with launch.exe replaced by FrontierOutpost.exe
```

Each project is flat: no `src/` and no `include/`. Former folders survive only as Filters
(`LTE\ScriptAPI`, `Game\Object`, …), so the IDE still groups files as before.

## 2. Decisions this plan takes (for approval)

| # | Decision | Why |
|---|---|---|
| D1 | The game's client side lives in `FrontierOutpost/`, the executable's project (owner, 2026-09-26). | The only place that may see both GameLogic and NeuronClient without breaking R9. |
| D2 | Every library links into the exe with `/WHOLEARCHIVE`. | LTSL functions, types and conversions register themselves in static constructors (`FreeFunction`, `DefineConversion`, …). A plain `.lib` link drops every object file nothing references, and that loses script functions without any error. |
| D3 | Legacy files are marked with `<Legacy>true</Legacy>` item metadata in their `.vcxproj`, and each such `ClCompile` carries ADR-001's settings (`/W3`, `/fp:fast`, no PCH, `/EHs`). `ProjectModel.py` reads the marker and the checkers skip those files. New files follow AGENTS.md in full, whatever project they are in. | Your answer on conformance. One place holds the list: the project file, which has to list every file anyway. |
| D4 | Output goes to `$(SolutionDir)$(Platform)\$(Configuration)\` for every project, as NeuronClient already does. `FrontierOutpost/Directory.Build.props` goes; its settings move to `Build/Legacy.props`, which the four projects import for their legacy items. | One output folder. `GameData/` is still found by walking up from the exe (ADR-004). |
| D5 | x64 and ARM64 both stay (ADR-006). | Nothing in the request changes them. |
| D6 | The exe stays a console program with the same arguments: `FrontierOutpost.exe <app> [--warp] [--frames N] [--capture path]`. | The smoke mode prints to stdout, and the README and the owner's runs use it. |
| D7 | Shaders go with the code that uses them: engine shaders (UI, post, SMAA, cubemap, SDF field, generic material) to `NeuronClient/Shaders/`, game shaders (planet, shield, wormhole, thruster, beam, …) to `FrontierOutpost/Shaders/`. There are two registries, one per project, and scripts still look shaders up by their legacy name through one lookup. `CheckProjectFiles.py`'s `REGISTRIES` gets both. | Your "each library's own Shaders/". ADR-008 is unchanged apart from where the files live. |
| D8 | The one-image build fixes the `Type_Get<T>` exe/dll split that `launch.cpp` works around with a raw cast. The cast goes. | It exists only because of the DLL boundary. |
| D9 | New ADRs: ADR-014 (the library split and layering, which supersedes ADR-005's layer list), ADR-015 (the per-file legacy exemption, which narrows ADR-001). ADR-001, -004, -005 and -008 get amendment notes. | AGENTS.md §6. |

## 3. How the untangle is done

The coupling today (measured from the include graph of `src/liblt`, 775 files):

- **Engine:** 50 of 290 LTE files are client code (Renderer, Shader*, Texture*, CubeMap,
  DrawState, Window, Keyboard, Mouse, Font, Viewport, RenderPass*, RenderStyle and their script
  bindings). The rest are render-free apart from 12 mixed files: `Mesh`, `Model`, `PlateMesh`,
  `ParticleSystem`, `SDFMesh`, `Profiler`, `Program`, `ProgramLog`, `Common.cpp` (closes the
  window on an assert), `Module/Scheduler` (times GPU work), `Module/Settings` (a UI widget), and
  `Volume/Array3D` (builds a Mesh).
- **Game:** 58 of 196 Game files, 3 of 82 Component files and 2 of 16 Module files include
  rendering, UI or input. The routes are:
  1. `ObjectT` virtuals: `OnDraw`, `OnDrawInterior`, `Begin/EndDrawInterior`, `GetRenderable`,
     and the `Drawable`/`Interior` component mixins that forward them.
  2. Item types build visuals when they are generated: `PlanetType` renders its surface cubemaps,
     `ShipType`/`StationType`/`TurretType` build Models from SDFs, and every `*Type` builds an
     Icon from UI glyphs.
  3. **Collision geometry comes from the visual.** `PhysicsEngine` asks an object's `Renderable`
     for `GetCollisionMesh()`.
  4. Pure client code: `Game/RenderPass/*`, `Game/Graphics/*`, `Materials`, `ShadingModels`,
     `Particles`, `Renderable/Starfield`, `Beam`, `Camera`, the XAudio2 sound adapter
     (`Module/SoundEngine*`, which follows the camera).
  5. Debug keys in simulation code (`System.cpp` F6, `PowerGenerator.cpp`).
  6. `Game/Object.h`, `Item.h`, `Items.h` and `Task.h` include UI headers for Icon and Widget
     members.

The pattern for all of them: **GameLogic keeps state, and the client derives what is shown from
that state.**

| Route | Untangled as |
|---|---|
| 1 Draw virtuals | Removed from `ObjectT` and the component mixins. A client-side `ObjectView` table in the exe, keyed by `ObjectType`, draws each type. The draw bodies move over nearly verbatim; the mixin order (which component draws first) is kept by walking the same component list the mixin chain encodes. |
| 2 Item visuals | GameLogic keeps each type's generation *parameters* (seeds, colors, SDF trees, which it already has). The client builds Models, textures and Icons from them on first use and caches them by item. `GetRenderable()`/`GetIcon()` become client functions of the item. |
| 3 Collision | NeuronCore gets a `Geometry` service interface: *give me the mesh of this SDF at this LOD*. NeuronClient implements it on the GPU (ADR-009's compute path, unchanged). GameLogic's physics asks the service for a collision mesh, keyed as `PhysicsEngine` keys it today, and never touches a `Renderable`. |
| 4 Pure client | Moves to `FrontierOutpost/` unchanged. |
| 5 Debug keys | The key test moves to the client; GameLogic exposes the action it triggered. |
| 6 UI members | Replaced by forward declarations or by an id the client resolves. |

Engine-side mixed files are split the same way: data and algorithms in NeuronCore, the
GPU half in a NeuronClient file named for it (`MeshBuffers.cpp`, `ModelDraw.cpp`, …).
`Renderable::Render` moves out of the NeuronCore base type in favor of a client-side renderer
of Renderables. `Common.cpp`'s assert and the scheduler's GPU timing call through a hook that the
client installs at startup.

**Risk, stated up front:** this is not a file move. Routes 1 to 3 change how legacy code is
wired together, in about 150 files. Behavior is held by §5's checks, not assumed.

## 4. Phases

Each phase ends building Debug and Release x64 through the solution, with the checkers green,
the tests passing, and §5's regression runs matching. Each phase is its own commit series on
one branch, and each commit builds.

**P0 Baseline.** Build `main` at Debug and Release x64. Record the warning counts and the
16 apps' smoke runs on the GPU: `launch.exe <app> --frames 120 --capture`, with timings and PNGs,
kept outside the repo.

**P1 Restructure, no code changes beyond includes.**
1. Create the NeuronCore, GameLogic, NeuronServer and FrontierOutpost projects and
   `Build/Legacy.targets`, and add them to the slnx with their references.
2. `git mv` every file to its library (§6) with its rename (§7), so `git log --follow` keeps the
   history.
3. Rewrite every `#include` to the flat name. The mapping comes from a script, and the build
   proves it.
4. `launch.cpp` becomes `FrontierOutpost/Main.cpp`. `LT_API` becomes empty. `lt.vcxproj` and
   `launch.vcxproj` go.
5. For P1 only, GameLogic may include NeuronClient headers. No other upward edge is allowed.
   *As done:* no NeuronCore file includes a NeuronClient header, but the 12 mixed engine files were
   not split. Their headers are in NeuronCore and their whole `.cpp` in NeuronClient, so NeuronCore
   still needs NeuronClient at link time. The split moved to P2 (§4.1, item 7). The one upward
   include from the renderer, `DrawState.cpp` reading the game clock, became a hook
   (`DrawState_SetGameTime`) that `Main.cpp` installs.
6. Update the checkers (`ProjectModel.py` exemption by marker, `REGISTRIES`, `TestCheckers.py`'s
   fixture trees), CI's test-suite search path, README, `.gitignore`, and the perf-review skill's
   paths. Write ADR-014 and ADR-015.

**P2 Game untangle.** Routes 1 to 6, one route per commit series, in the order 4, 6, 5, 1, 2, 3.
Game shaders move to `FrontierOutpost/Shaders/` with the code that uses them. The phase ends when
GameLogic.vcxproj loses NeuronClient's include path and `Tests/GameLogicTests` links.

**P3 Close-out.** ARM64 Debug and Release build. A full run of all 16 apps, looked at on screen and
not only captured. Archive this plan to `Design/Archive/`.

### 4.1 What is left (2026-09-26, after P1 landed as `a0123e0`)

P2, in this order. Each item is its own commit series, and each ends building, tested and
smoke-run against P0:

- [ ] 1. **Pure client code to the exe** (route 4): `Game/RenderPass/*`, `Game/Graphics/*`,
  `Materials`, `ShadingModels`, `Particles`, `Renderable/Starfield`, `Beam`, `Camera`, the camera
  script bindings, and `SoundEngineXAudio2.cpp`, whose part without game types can move down to
  NeuronClient.
- [ ] 2. **UI members out of game headers** (route 6): `Object.h`, `Item.h`, `Items.h`, `Task.h`.
- [ ] 3. **Debug keys out of simulation code** (route 5): `System.cpp` F6, `PowerGenerator.cpp`.
- [ ] 4. **Draw virtuals off `ObjectT` and the component mixins** (route 1): replaced by an
  `ObjectView` table in the exe, keyed by `ObjectType`, which keeps the order components draw in.
- [ ] 5. **Item visuals built by the client** (route 2): planet textures, ship, station and turret
  Models, and item Icons, built from parameters GameLogic keeps.
- [ ] 6. **Collision through a NeuronCore geometry service** (route 3), which NeuronClient implements
  on the GPU. Physics stops reading `Renderable`.
- [ ] 7. **The 12 mixed engine files split**: `Mesh`, `Model`, `PlateMesh`, `ParticleSystem`,
  `SDFMesh`, `Profiler`, `Program`, `ProgramLog`, `Common.cpp`, `Scheduler`, `Settings` and
  `Array3D`. Each gets its GPU-free half in NeuronCore and its GPU half in NeuronClient.
  `Renderable::Render` leaves the NeuronCore base type.
- [ ] 8. **Game shaders to `FrontierOutpost/Shaders/`,** with a second registry in the exe and one
  lookup by legacy name.
- [ ] 9. **`Tests/GameLogicTests`**, linking NeuronCore, GameLogic and NeuronServer only. Then
  GameLogic.vcxproj drops NeuronClient from its include path and its references.

P3:

- [ ] 10. ARM64 Debug and Release builds.
- [ ] 11. All 16 apps run interactively and looked at.
- [ ] 12. This plan moves to `Design/Archive/`.

Outside the plan: about 180 files still name liblt or `lt.dll` in comments, and `widget` has no
`Main` and crashes on exit (it did on `main` before P1 too).

## 5. Verification

- **Build:** Debug and Release x64 after every phase, ARM64 at P3. The warning count per
  project is compared with P0; legacy warnings may move between projects but not grow.
- **Checkers:** `CheckFormat.py`, `CheckProjectFiles.py`, `RunClangTidy.py` (from a Developer
  PowerShell) and `TestCheckers.py`.
- **Tests:** NeuronClientTests, and GameLogicTests from P2.
- **Behavior:** the 16 apps' smoke runs compared against P0. A PNG that differs is investigated
  before the phase closes. SDF shapes and procedural content are seeded, so an unchanged app
  should produce the same frame on the same GPU.
- **Looked at:** `war` and `ltheory` run interactively at the end of P1 and P2 (AGENTS.md §3:
  a green build says nothing about whether it draws).

## 6. Where each file goes (P1)

| From `FrontierOutpost/src/liblt/` | To |
|---|---|
| `Common.h`, `BuildMode.h`, `Common.cpp` (split) | NeuronCore |
| `LTE/**`, except the client set below | NeuronCore |
| `LTE/` client set: `Renderer*`, `Shader*`, `ShaderRegistry`, `Texture2D/3D`, `CubeMap`, `DrawState`, `Window`, `Keyboard`, `Mouse`, `Font`, `Viewport`, `RenderPass*`, `RenderStyle`, `RenderPass/*`, and `ScriptAPI/{Keyboard,Model,Renderer,ShaderInstance,Texture2D}` | NeuronClient |
| `LTE/{Mesh,Model,PlateMesh,ParticleSystem,SDFMesh,Profiler,Program,ProgramLog}.cpp` | split: NeuronCore + NeuronClient |
| `Volume/*`, `ThirdParty/{KDTree,TriTriOverlap}` | NeuronCore |
| `ThirdParty/SMAA_*Tex.h` | NeuronClient |
| `Module/{Scheduler,FrameTimer}` | NeuronCore (the scheduler's GPU timing through a hook) |
| `Module/{Settings,SettingsEntry}`, `UI/**` | NeuronClient |
| `Module/{SoundEngine,Sound}`, `Module/SoundEngine/XAudio2.cpp`, `Module/ScriptAPI/SoundEngine.cpp` | FrontierOutpost (the game's sound adapter) |
| `Module/PhysicsEngine` | GameLogic |
| `Game/**`, `Component/**`, `AI/**` | GameLogic (P1); the client halves go to FrontierOutpost in P2 |
| `Shaders/*.hlsl`, `SmaaLicense.txt` | NeuronClient/Shaders (P1); game shaders go to FrontierOutpost/Shaders in P2 |
| `FrontierOutpost/include/UTF8/*`, `windirent.h` | NeuronCore (`Utf8Core.h`, `Utf8Unchecked.h`, `windirent.h`), with their licence text |
| `FrontierOutpost/src/launch/launch.cpp`, `src/resources.rc`, `src/resource.h` | FrontierOutpost (`Main.cpp`, `Resources.rc`, `Resource.h`) |

## 7. Renames

Rule: a file is renamed when its name clashes after flattening. That means its stem matches
another folder's file, a CRT/SDK header (`String.h`, `Math.h`, `Time.h`, `Segment.h`,
`Effects.h`, `Types.h`), or a NeuronClient file (`Window`, `Program`). The file nearest the root
keeps the name when it is alone at that depth. Every other one takes its former folder as a prefix,
spelled per R4 (`LTE`→`Lte`, `ScriptAPI`→`ScriptApi`, `SDF`→`Sdf`, `UI`→`Ui`, `AI`→`Ai`). A
`.h`/`.cpp` pair is renamed together. That gives 111 renames, with no second-order clash. For
example:

| Before | After |
|---|---|
| `LTE/String.h/.cpp`, `LTE/Math.h/.cpp`, `LTE/Time.h/.cpp` | `LteString`, `LteMath`, `LteTime` |
| `LTE/Window.h/.cpp`, `LTE/Program.h/.cpp` | `LteWindow`, `LteProgram` |
| `LTE/Common.h`, `Game/Common.h`, `UI/Common.h`, … | `LteCommon.h`, `GameCommon.h`, `UiCommon.h`, … (the root `Common.h` keeps its name) |
| `LTE/ScriptAPI/V2.cpp`, `UI/ScriptAPI/Widget.cpp`, … | `ScriptApiV2.cpp`, `ScriptApiWidget.cpp`, … |
| `UI/Widget/Custom.cpp`, `LTE/Warp/Custom.cpp`, `Game/Object/Custom.cpp` | `WidgetCustom.cpp`, `WarpCustom.cpp`, `ObjectCustom.cpp` |
| `LTE/SDF/Sphere.cpp` (next to an unrelated `Sphere.h`) | `SdfSphere.cpp` |
| `Game/Attribute/*.h` | `Attribute*.h` |
| `LTE/Function/Call.h`, `LTE/Expression/FunctionCall.cpp` | `FunctionCall.h`, `ExpressionFunctionCall.cpp` (resolved by hand) |

P1 prints the complete table to its ADR, generated by the same script that performs the moves.

## 8. Open for the owner

1. **A server's SDF geometry needs a GPU.** ADR-009 forecloses building fields on the CPU, and
   collision meshes come from those fields. This plan puts geometry behind an interface, so
   GameLogic is independent of how it is made. A NeuronServer implementation (WARP, or a CPU port
   of the field interpreter) is a decision for when the server is designed, and is not part of
   this work.
2. **The game's client side in the exe project** (D1). The alternative is a fifth library,
   `GameClient.lib`, which you did not list.
