# ADR-013: Unused material is removed first, by reachability from the kept apps

- **Status:** Accepted (owner, 2026-09-25, after the NeuronClient migration's Phase 1 removals)
- **Scope:** apps, code, render passes, shaders and assets in `FrontierOutpost.slnx`,
  `FrontierOutpost/` and `GameData/` that nothing kept reaches
- **Detail:** `Design/Plan/NeuronClient-migration.md` §1, §3 (point 3), §5.6, §6 (Phase 1), §9,
  N0, N4, N6, N9 and N14. Engine paths are in `FrontierOutpost/src/liblt/`. The lists below are
  candidates: Phase 1 re-verifies each one and records here what went.

## Context

N0 removes unused features, and N4 extends that to all four tiers of unused material: toy and test
apps, unreachable code, dead passes and shaders, and unused assets. What goes is not ported: after
Phase 1, 130 of the 169 shader files remain to port (plan §4.2, §5.6).

Removal comes first, while OpenGL still renders, so a fault it causes cannot be the new renderer's
(plan §1, §6). No tag marks the OpenGL build it starts from (N13).

## Decision

1. **Something goes when nothing kept reaches it,** through C++ callers or script callers.
   Reachability starts at `launch.exe` and the 16 kept apps. Decision 8 qualifies this for the
   script API.
2. **Apps go first,** and the analysis is then re-run, because removing the apps shrinks what
   everything else reaches (plan Phase 1 step 1).
3. **Then one commit per tier:** code, then passes and shaders, then assets. All four builds pass
   after each (plan §6, Phase 1 step 2).
4. **Every candidate is re-verified before it is deleted.** The lists come from the 2026-09-25
   survey (plan §9). When Phase 1 is done, this ADR records what actually went (plan Phase 1
   step 3).
5. **Sounds are checked, not removed on the survey's word** (plan §9 D). The analysis lists every
   sound that code or scripts name and `GameData/sound` has no WAV for; that list, not the log, is
   how N6's warnings stay visible. It also parses every WAV that is there by the engine's rules
   (PCM, IEEE float or MS-ADPCM), so a file that would stop the game (N9) is found first.
   `Build/CheckSounds.py` does both, and CI runs it; it fails only on a file that cannot be
   played.
6. **Phase 1 is done** when all four builds pass, the kept apps start, and nothing refers to a
   removed name (plan Phase 1).
7. **The candidates** (plan §9):
   - **Apps, 12 of 28:** `prime`, `sandbox`, `threads`, `font`, `draw`, `brain`, `hnn`, `life`
     (with `App/Life/`), `universe`, `strukt`, `colony` and `launcher`, and whatever only they
     reach. Kept: `war`, `dogfight`, `rails`, `ltheory`, `handling`, `observatory`, `model`,
     `platemesh`, `map`, `market`, `objectinfo`, `hud`, `ui`, `image`, `loading`, and the `widget`
     host.
   - **Unreachable code:** `FrontierOutpost/src/old/`, seven programs no project builds; the HTTP
     path (`LocationWeb`, `Location_Web`, and with them `sfml-network` and `ws2_32`); joysticks
     (`Joystick.*`, their buttons and axes, and the polling in `LTE/Program.cpp`); the C++ HUD
     widget `Game/Widget/HUD.cpp`, with `Settings_Button` and the wiring only it uses;
     `Config.cpp`; the archive path (`kUseArchive` is false); the Telemetry profiler branch;
     `Audio/`; `Network/`; `CodeGen/CodeBlock.h`; `CodeObject_Custom`; and `sfml-audio` and
     `sfml-main`, which nothing links.
   - **Dead functions:** the window setters for icon, position, fullscreen, cursor and capture;
     `Mouse_SetPos`, `GetX`, `GetY`, `GetIdleTime`, `GetDX`, `GetDY` and `GetDP`; `GetKeyChar`,
     `Keyboard_Block`, `Keyboard_IsBlocked`, `Keyboard_System` and `KeyWithModifiers`;
     `CubeMap::SaveTo` and `Texture_Atlas`; `SoundEngine_Null` and `CreatePhysicsEngineNull`;
     `Renderer_DrawQuadOutline` and `GLU::*`.
   - **Vendored headers nothing includes,** under `FrontierOutpost/include/`: `OVR/`, `enet/`,
     `GL/glut.h`, `GL/glui.h`, `GL/GLAux.h`, `UTF8/checked.h`, `Glew/GL/wglew.h` and
     `Glew/GL/glxew.h`.
   - **Dead passes:** SSAO, DustClouds, MotionBlur, RadialBlur, Aberration, BloomLight, Composite
     and ClearDepth, and the imposter renderable.
   - **Shader files:** 36, 983 lines in all. 27 are referenced by nothing; 9 are reached only by
     dead code, the broken `ui/rect` and `solidcolor` among them.
   - **SDF node types that nothing constructs,** `FractalPerlin` among them (ADR-009). Scripts
     call 3 of the 20 constructors; C++ callers decide the rest.
   - **Fonts:** 34 families, NotoSans, NotoSansCJKsc and Play among them, and the `FontPreview`
     and `SplashScreen` widgets that nothing opens. Rajdhani, Iceland, SourceCodePro and Gafata
     stay, with their licences (ADR-010).
   - **Textures:** `icon.png` and `splash.png`.
8. **The script API is an interface, not unused material** (N14). A native that nothing reaches
   goes only when it is the only way into code of its own, and that code goes with it. A member
   of a family that one macro builds per object type, item field, component or key, and a short
   native over code that stays, remain whether or not a script calls them today.

## What went (Phase 1, 2026-09-25)

Every candidate above was re-verified against the tree before it went, by reachability from
`launch.exe`, the 16 kept apps and every script C++ loads by name. `Build/CheckSounds.py` decided
the sounds. A clang syntax pass over `lt` and `launch` checked each code commit before it was
pushed, and CI's four-way build after.

- **Apps** (`04d440b`): the 12 listed, with `App/Life/`. 16 stay, and they reach all 121 remaining
  scripts.
- **Code** (`788983a`): the list, with these differences.
  - More went than listed:
    - the whole Button and Axis binding system, which only the C++ HUD used;
    - `Config.cpp`, which nothing called, and so `cache/config.txt`, which nothing wrote
      (ADR-004);
    - all of `Strukt/` and `CodeGen/`, not only `CodeObject_Custom` and `CodeBlock.h`;
    - SFML's Audio, Main and Network modules, and the `extlibs` libraries and headers nothing
      used (ADR-002);
    - the scripts `Common/Grammar`, `Task/Test` and `Widget/RadialButton`;
    - the Thread and StringList script APIs, which only the removed apps used.
  - Less went than listed: `GLU::CreateTexture2D`, which `Texture2D` uses, and GLEW's `wglew.h`
    and `glxew.h`, which `glew.c` includes. They go with OpenGL and GLEW in Phase 4.
    `KeyWithModifiers` is not in the tree.
- **Passes, shaders and SDF nodes** (`e75f064`):
  - the eight passes and the imposter renderable;
  - the rect glyph, the flat-colour shading model and two materials nothing called;
  - 38 shader files, 1,011 lines, leaving 131 of 169;
  - 15 SDF node types, leaving 9: Cylinder, FractalWorley, Radial, RoundBox, Scale, Shell,
    Sphere, Subtract and Torus.

  `ShadingModel_Debug` stays: `Materials.cpp`'s `kDebug` switch uses it.
- **Assets** (`f20c684`):
  - 34 font families;
  - the `FontPreview` and `SplashScreen` widgets;
  - `icon.png` and `splash.png`;
  - 31 WAV and 55 Ogg sounds that nothing names, leaving 20 WAV files and the 24 Ogg files that
    are to be converted (FrontierOutpost/MIGRATION_NOTES.md O10);
  - 52 of the 53 files in `gamedata/`, which the survey did not list. Only
    `grammar/default.txt` is read.

  `GameData/` holds 324 files, 54.9 MB, where it held 633 files, 283.7 MB. Both are sums of
  `git ls-tree -r -l` over `GameData/`, so they count `.gitattributes` and the two Noto licences
  that came after ADR-004's 630.
- **Script natives** (`b78a33f`, N14). The engine registered 1,045 natives, counting each member of
  a macro-built family. 609 were reached from a remaining script: 526 by name, and 83 only through
  an operator such as `+` or `==`, counted as reached when any script uses the operator, since the
  overload it resolves to depends on types. 171 more were named in C++ and by no script. The other
  265 were reached by neither, and 12 of them were the only way into code of their own. Those 12
  went, with what only they reached:
  - the C++ warp rail, whose model nothing built, with `warprail.jsl` and `Mesh_Cylinder` (the kept
    scripts build rails with `Object/WarpRail.lts`);
  - the Custom, Drill, LOD and Patrol tasks, and the deposit event;
  - the three workers, with the skill getters on `ItemT` that nothing called;
  - the construction drone type, with `Renderable_Ice`, which only it used;
  - the custom compositor, and the settings widget with `SettingsEntry::GetWidget`.

  730 lines went, 35 of them the shader, leaving 130 shader files. The 253 natives that stay are
  194 family members and 59 short ones, none over 11 lines (decision 8). Measured by preprocessing
  every `lt` and `launch` source, reading each `Function_Create` and `Function_AddAlias` the
  registration macros expand to, and matching both against the scripts' tokens as
  `StringList_ParseLine` splits them. The same count after the removal gives 1,032 natives, 253 of
  them reached by nothing.

## What this forecloses

- **Porting any confirmed item** to Direct3D 12, HLSL or NeuronClient.
- **Deleting anything a kept app still reaches,** whatever the survey said.
- **Keeping unused material for later,** the script API's natives aside (decision 8). What goes
  stays in the history, and nothing else keeps it.
