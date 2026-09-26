# ADR-014: One executable over four libraries

- **Status:** Accepted (owner, 2026-09-26)
- **Scope:** the solution's projects and the edges between them. It supersedes ADR-005's layer
  list (point 3: `launch.exe`, then `lt.dll`, then `NeuronClient.lib`). The rest of ADR-005 still
  stands for NeuronClient's own code.
- **Detail:** `Design/LibrarySplit-plan.md`

## Context

The game was `launch.exe`, which loaded `lt.dll` (liblt, the imported engine and game), which
linked `NeuronClient.lib`. The owner wants a single executable that is both client and, later,
server, over libraries split by who uses them. They also want flat project folders (AGENTS.md §2),
with no `src/` or `include/`, and names made unique by renaming where flattening makes them clash.

## Decision

1. **Projects:**
   - `FrontierOutpost.exe`: the only executable. It takes the launcher's arguments unchanged
     (`<app> [--warp] [--frames N] [--capture path]`) and holds the game's client side, meaning code
     that needs both GameLogic and NeuronClient.
   - `NeuronCore.lib`: the engine both sides share.
   - `NeuronClient.lib`: rendering, shaders, window, input, glyphs, images, sound and the UI toolkit.
   - `NeuronServer.lib`: the server's side. It is a placeholder today.
   - `GameLogic.lib`: the game's rules and state.
2. **Edges run one way:**
   - NeuronCore sees nothing of the others.
   - NeuronClient and NeuronServer see NeuronCore only.
   - GameLogic sees NeuronCore. Until the plan's P2 it also sees NeuronClient, and P2 removes that
     edge.
   - The exe sees all four.
3. **The exe links every library with `/WHOLEARCHIVE`.** LTSL functions, types and conversions
   register themselves from static constructors (`FreeFunction`, `DefineConversion`, …). A plain
   library link keeps only the objects something names, so it would drop them without an error.
4. **`LT_API` is empty.** It exported liblt's symbols from the DLL. With one image there is no
   boundary, so `Type_Get<T>` no longer has one copy of its storage per module.
5. **Flat folders, and a rename where a name would clash.** A file is renamed when its stem matches
   another folder's file, a CRT or SDK header (`String.h`, `Math.h`, `Time.h`, `Segment.h`,
   `Effects.h`, `Types.h`), or a NeuronClient file (`Window`, `Program`). The file nearest the root
   keeps the name when it is alone at that depth. Every other one takes its former folder as a
   prefix (`LTE`→`Lte`, `ScriptAPI`→`ScriptApi`, `SDF`→`Sdf`, `UI`→`Ui`, `AI`→`Ai`). Former folders
   survive as the projects' Filters. The full list is below.
6. **Shaders live in the Shaders/ folder of the library that uses them.** For now that is
   `NeuronClient/Shaders/`. The game's own shaders move to `FrontierOutpost/Shaders/` with the
   game's drawing code in P2.
7. **Output goes to `$(SolutionDir)$(Platform)\$(Configuration)\`,** so the exe is
   `x64\<Configuration>\FrontierOutpost.exe`. It still finds `GameData/` by walking up (ADR-004).
8. **x64 and ARM64 for every project** (ADR-006).

## What this forecloses

- A DLL, and a second executable, for the game.
- An edge from NeuronClient, NeuronServer or NeuronCore to GameLogic, or from any library to the
  exe.
- `src/`, `include/` or any other subfolder in a project, apart from `Shaders/` and
  `CompiledShaders/`.

## Renames

The file and its old path under `FrontierOutpost/src/liblt/` (or `FrontierOutpost/`), and the new
path. A file not listed kept its name.

| Before | After |
|---|---|
| `FrontierOutpost/include/UTF8/core.h` | `NeuronCore/Utf8Core.h` |
| `FrontierOutpost/include/UTF8/unchecked.h` | `NeuronCore/Utf8Unchecked.h` |
| `FrontierOutpost/src/launch/launch.cpp` | `FrontierOutpost/Main.cpp` |
| `AI/Types.h` | `GameLogic/AiTypes.h` |
| `Component/Common.h` | `GameLogic/ComponentCommon.h` |
| `Component/Resources.cpp` | `GameLogic/ComponentResources.cpp` |
| `Component/Resources.h` | `GameLogic/ComponentResources.h` |
| `Component/Tasks.cpp` | `GameLogic/ComponentTasks.cpp` |
| `Component/Tasks.h` | `GameLogic/ComponentTasks.h` |
| `Game/Action/Mine.cpp` | `GameLogic/ActionMine.cpp` |
| `Game/Attribute/Capability.h` | `GameLogic/AttributeCapability.h` |
| `Game/Attribute/Color.h` | `GameLogic/AttributeColor.h` |
| `Game/Attribute/Common.h` | `GameLogic/AttributeCommon.h` |
| `Game/Attribute/Damage.h` | `GameLogic/AttributeDamage.h` |
| `Game/Attribute/Hash.h` | `GameLogic/AttributeHash.h` |
| `Game/Attribute/Icon.h` | `GameLogic/AttributeIcon.h` |
| `Game/Attribute/Integrity.h` | `GameLogic/AttributeIntegrity.h` |
| `Game/Attribute/Object.h` | `GameLogic/AttributeObject.h` |
| `Game/Attribute/Renderable.h` | `GameLogic/AttributeRenderable.h` |
| `Game/Attribute/Scale.h` | `GameLogic/AttributeScale.h` |
| `Game/Attribute/Sockets.h` | `GameLogic/AttributeSockets.h` |
| `Game/Attribute/Sound.h` | `GameLogic/AttributeSound.h` |
| `Game/Attribute/Task.h` | `GameLogic/AttributeTask.h` |
| `Game/Attribute/Traits.h` | `GameLogic/AttributeTraits.h` |
| `Game/Attribute/Value.h` | `GameLogic/AttributeValue.h` |
| `Game/Common.h` | `GameLogic/GameCommon.h` |
| `Game/Event/Damage.cpp` | `GameLogic/EventDamage.cpp` |
| `Game/Graphics/Effects.cpp` | `GameLogic/GraphicsEffects.cpp` |
| `Game/Graphics/Effects.h` | `GameLogic/GraphicsEffects.h` |
| `Game/Object/Asteroid.cpp` | `GameLogic/ObjectAsteroid.cpp` |
| `Game/Object/Custom.cpp` | `GameLogic/ObjectCustom.cpp` |
| `Game/Object/Dynamic.cpp` | `GameLogic/ObjectDynamic.cpp` |
| `Game/Object/Static.cpp` | `GameLogic/ObjectStatic.cpp` |
| `Game/RenderPass/Camera.cpp` | `GameLogic/RenderPassCamera.cpp` |
| `Game/RenderPass/Particles.cpp` | `GameLogic/RenderPassParticles.cpp` |
| `Game/RenderPasses.h` | `GameLogic/GameRenderPasses.h` |
| `Game/Renderable/Asteroid.cpp` | `GameLogic/RenderableAsteroid.cpp` |
| `Game/ScriptAPI/Camera.cpp` | `GameLogic/ScriptApiCamera.cpp` |
| `Game/ScriptAPI/Item.cpp` | `GameLogic/ScriptApiItem.cpp` |
| `Game/ScriptAPI/Object.cpp` | `GameLogic/ScriptApiObject.cpp` |
| `Game/ScriptAPI/Player.cpp` | `GameLogic/ScriptApiPlayer.cpp` |
| `Game/ScriptAPI/Task.cpp` | `GameLogic/ScriptApiTask.cpp` |
| `Game/Settings.h` | `GameLogic/GameSettings.h` |
| `Game/Task/Mine.cpp` | `GameLogic/TaskMine.cpp` |
| `Game/Tasks.h` | `GameLogic/GameTasks.h` |
| `LTE/Common.h` | `NeuronCore/LteCommon.h` |
| `LTE/Expression/Array.cpp` | `NeuronCore/ExpressionArray.cpp` |
| `LTE/Expression/Cast.cpp` | `NeuronCore/ExpressionCast.cpp` |
| `LTE/Expression/Function.cpp` | `NeuronCore/ExpressionFunction.cpp` |
| `LTE/Expression/FunctionCall.cpp` | `NeuronCore/ExpressionFunctionCall.cpp` |
| `LTE/Expression/List.cpp` | `NeuronCore/ExpressionList.cpp` |
| `LTE/Expression/Reference.cpp` | `NeuronCore/ExpressionReference.cpp` |
| `LTE/Expression/Type.cpp` | `NeuronCore/ExpressionType.cpp` |
| `LTE/Function/Call.h` | `NeuronCore/FunctionCall.h` |
| `LTE/Function/Cast.h` | `NeuronCore/FunctionCast.h` |
| `LTE/Function/Pointer.h` | `NeuronCore/FunctionPointer.h` |
| `LTE/Function/Value.h` | `NeuronCore/FunctionValue.h` |
| `LTE/Math.cpp` | `NeuronCore/LteMath.cpp` |
| `LTE/Math.h` | `NeuronCore/LteMath.h` |
| `LTE/Program.cpp` | `NeuronClient/LteProgram.cpp` |
| `LTE/Program.h` | `NeuronClient/LteProgram.h` |
| `LTE/RenderPasses.cpp` | `NeuronClient/LteRenderPasses.cpp` |
| `LTE/RenderPasses.h` | `NeuronClient/LteRenderPasses.h` |
| `LTE/SDF/Box.cpp` | `NeuronCore/SdfBox.cpp` |
| `LTE/SDF/Cylinder.cpp` | `NeuronCore/SdfCylinder.cpp` |
| `LTE/SDF/Scale.cpp` | `NeuronCore/SdfScale.cpp` |
| `LTE/SDF/Sphere.cpp` | `NeuronCore/SdfSphere.cpp` |
| `LTE/ScriptAPI/Data.cpp` | `NeuronCore/ScriptApiData.cpp` |
| `LTE/ScriptAPI/Expression.cpp` | `NeuronCore/ScriptApiExpression.cpp` |
| `LTE/ScriptAPI/Float.cpp` | `NeuronCore/ScriptApiFloat.cpp` |
| `LTE/ScriptAPI/Grammar.cpp` | `NeuronCore/ScriptApiGrammar.cpp` |
| `LTE/ScriptAPI/Keyboard.cpp` | `NeuronClient/ScriptApiKeyboard.cpp` |
| `LTE/ScriptAPI/List.cpp` | `NeuronCore/ScriptApiList.cpp` |
| `LTE/ScriptAPI/Mesh.cpp` | `NeuronCore/ScriptApiMesh.cpp` |
| `LTE/ScriptAPI/Model.cpp` | `NeuronClient/ScriptApiModel.cpp` |
| `LTE/ScriptAPI/OS.cpp` | `NeuronCore/ScriptApiOS.cpp` |
| `LTE/ScriptAPI/PlateMesh.cpp` | `NeuronCore/ScriptApiPlateMesh.cpp` |
| `LTE/ScriptAPI/RNG.cpp` | `NeuronCore/ScriptApiRNG.cpp` |
| `LTE/ScriptAPI/Ray.cpp` | `NeuronCore/ScriptApiRay.cpp` |
| `LTE/ScriptAPI/Renderable.cpp` | `NeuronCore/ScriptApiRenderable.cpp` |
| `LTE/ScriptAPI/Renderer.cpp` | `NeuronClient/ScriptApiRenderer.cpp` |
| `LTE/ScriptAPI/ShaderInstance.cpp` | `NeuronClient/ScriptApiShaderInstance.cpp` |
| `LTE/ScriptAPI/String.cpp` | `NeuronCore/ScriptApiString.cpp` |
| `LTE/ScriptAPI/StringTree.cpp` | `NeuronCore/ScriptApiStringTree.cpp` |
| `LTE/ScriptAPI/Texture2D.cpp` | `NeuronClient/ScriptApiTexture2D.cpp` |
| `LTE/ScriptAPI/Timer.cpp` | `NeuronCore/ScriptApiTimer.cpp` |
| `LTE/ScriptAPI/V2.cpp` | `NeuronCore/ScriptApiV2.cpp` |
| `LTE/ScriptAPI/V3.cpp` | `NeuronCore/ScriptApiV3.cpp` |
| `LTE/ScriptAPI/V4.cpp` | `NeuronCore/ScriptApiV4.cpp` |
| `LTE/Segment.h` | `NeuronCore/LteSegment.h` |
| `LTE/String.cpp` | `NeuronCore/LteString.cpp` |
| `LTE/String.h` | `NeuronCore/LteString.h` |
| `LTE/Time.cpp` | `NeuronCore/LteTime.cpp` |
| `LTE/Time.h` | `NeuronCore/LteTime.h` |
| `LTE/Type/Array.cpp` | `NeuronCore/TypeArray.cpp` |
| `LTE/Type/Pointer.cpp` | `NeuronCore/TypePointer.cpp` |
| `LTE/Types.h` | `NeuronCore/LteTypes.h` |
| `LTE/Warp/Custom.cpp` | `NeuronCore/WarpCustom.cpp` |
| `LTE/Window.cpp` | `NeuronClient/LteWindow.cpp` |
| `LTE/Window.h` | `NeuronClient/LteWindow.h` |
| `Module/Common.h` | `NeuronCore/ModuleCommon.h` |
| `Module/ScriptAPI/SoundEngine.cpp` | `GameLogic/ScriptApiSoundEngine.cpp` |
| `Module/Settings.cpp` | `NeuronClient/ModuleSettings.cpp` |
| `Module/Settings.h` | `NeuronClient/ModuleSettings.h` |
| `Module/SoundEngine/XAudio2.cpp` | `FrontierOutpost/SoundEngineXAudio2.cpp` |
| `UI/Common.h` | `NeuronClient/UiCommon.h` |
| `UI/Glyph/Box.cpp` | `NeuronClient/GlyphBox.cpp` |
| `UI/ScriptAPI/Glyph.cpp` | `NeuronClient/ScriptApiGlyph.cpp` |
| `UI/ScriptAPI/Icon.cpp` | `NeuronClient/ScriptApiIcon.cpp` |
| `UI/ScriptAPI/Interface.cpp` | `NeuronClient/ScriptApiInterface.cpp` |
| `UI/ScriptAPI/Widget.cpp` | `NeuronClient/ScriptApiWidget.cpp` |
| `UI/Widget/Custom.cpp` | `NeuronClient/WidgetCustom.cpp` |
| `UI/Widget/Dynamic.cpp` | `NeuronClient/WidgetDynamic.cpp` |
| `UI/Widget/List.cpp` | `NeuronClient/WidgetList.cpp` |
| `UI/Widget/Stack.cpp` | `NeuronClient/WidgetStack.cpp` |
| `Volume/Common.h` | `NeuronCore/VolumeCommon.h` |
| `FrontierOutpost/src/resource.h` | `FrontierOutpost/Resource.h` |
| `FrontierOutpost/src/resources.rc` | `FrontierOutpost/Resources.rc` |
