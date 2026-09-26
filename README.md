# Limit Theory Old

This is the (old) C++ implementation of Limit Theory, the Limit Theory Engine (LTE), and the Limit Theory Scripting Language (LTSL), written from 2012 to 2015. While this code is dated compared to the newer C/Lua LT, it is arguably meatier in gameplay implementation.

# Requirements

Although LT was developed for both Windows and Linux (indeed, primarily developed on Linux), I have only resurrected the Windows build. If you would like to get it working on Linux, you're on your own...although it shouldn't be too difficult.

# Prerequisites

To build Limit Theory, you'll need a few standard developer tools. All of them are available to download for free.

- Git: https://git-scm.com/downloads
- Visual Studio 2026 Community, with the "Desktop development with C++" workload, and its MSVC ARM64 build tools to build for ARM64: https://visualstudio.microsoft.com/vs/

The game runs on Windows 10 or later, on x64 or ARM64, with a GPU that runs Direct3D 12 at feature level 11_0.

# Building

FrontierOutpost builds with MSBuild from `FrontierOutpost.slnx`, at the root of this repository. The solution builds one executable, `FrontierOutpost.exe`, from four static libraries: NeuronCore (the engine both sides share), NeuronClient (rendering, shaders, window, input, glyphs, images and sound), NeuronServer (the server's side, a placeholder today) and GameLogic (the game's rules and state), with NeuronClient's tests. How they depend on each other is ADR-014; the imported Limit Theory code among them keeps its original conventions, file by file (ADR-015). The decisions that shape it are the ADRs in `Design/ADR/`. How it got here, from the original's CMake build and from OpenGL onto NeuronClient and Direct3D 12, is recorded in `Design/Archive/`.

## Compiling

Open a **Developer PowerShell for Visual Studio 2026** at the root of the repository, and run

- `msbuild FrontierOutpost.slnx /m /p:Configuration=Release /p:Platform=x64`

Use `Configuration=Debug` for a debug build, and `Platform=ARM64` to build for ARM64. Opening `FrontierOutpost.slnx` in Visual Studio 2026 works too. The binaries land in `FrontierOutpost/bin/<Platform>/<Configuration>/`, and NeuronClient's in `<Platform>/<Configuration>/`.

## Testing

NeuronClient's tests are in `Tests/NeuronClientTests/`. After a Debug build, run them in the same Developer PowerShell:

- `vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64`

They draw on WARP, Windows' software renderer, with the Direct3D 12 debug layer, which comes with Windows' optional Graphics Tools, so they need no GPU.

## Getting the Assets

Everything LT loads at run time (fonts, sounds, textures, game data and LTSL scripts) is in `GameData/`, at the root of this repository, as ordinary files. The shaders are HLSL, in `NeuronClient/Shaders/`, and the build compiles them into the executable. The game plays WAV sounds only, and every sound in `GameData/sound/` is one. A sound the code or a script names but that has no file plays silence, and the log names it once as a warning; `Build/CheckSounds.py` lists any such sound, and checks that every WAV file can be played.

## Running an LTSL App

If the compilation is successful, you now have `FrontierOutpost.exe`, which is the only executable. This program launches an LTSL script. The intention was for Limit Theory (and all mods) to be broken into many LTSL scripts, which would then implement the gameplay, using script functions exposed by the underlying engine.

It finds `GameData/` by itself, in its own folder or the nearest folder above it, so it can be started from anywhere. From the root of the repository, give it the script's name:

- `x64\Release\FrontierOutpost.exe <script_name_without_extension>`

All top-level scripts are in the `GameData/script/App` directory. So you can do, for example:

- `x64\Release\FrontierOutpost.exe war`

To run the app 'war.lts', which is an AI skirmish test. The 15 apps there all start and draw, but many are tests or tools rather than a game, and some work enough to allow you to fly around in a system. `widget.lts` is not an app: it is the host the others open their widgets in.

`FrontierOutpost.exe` also takes three options for a smoke run, which checks an app with nobody watching. `--frames N` runs the app for N frames and quits, `--capture <path>` saves the last of them as a PNG, and `--warp` draws on WARP, with the Direct3D 12 debug layer on and nothing shown. With `--frames`, nothing waits for a dialog to be answered, and the exit code is 1 when the app fails, stops short of its frames, or the debug layer reports an error. For example:

- `x64\Debug\FrontierOutpost.exe war --warp --frames 30 --capture war.png`

# Example of the Entire Process

An example of the entire sequence of commands to build and run an LTSL app, in a Developer PowerShell for Visual Studio 2026 at the root of this repository:

```
msbuild FrontierOutpost.slnx /m /p:Configuration=Release /p:Platform=x64
x64\Release\FrontierOutpost.exe war
```
