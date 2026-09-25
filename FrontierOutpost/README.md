# Limit Theory Old

This is the (old) C++ implementation of Limit Theory, the Limit Theory Engine (LTE), and the Limit Theory Scripting Language (LTSL), written from 2012 to 2015. While this code is dated compared to the newer C/Lua LT, it is arguably meatier in gameplay implementation.

# Requirements

Although LT was developed for both Windows and Linux (indeed, primarily developed on Linux), I have only resurrected the Windows build. If you would like to get it working on Linux, you're on your own...although it shouldn't be too difficult.

# Prerequisites

To build Limit Theory, you'll need a few standard developer tools. All of them are available to download for free.

- Git: https://git-scm.com/downloads
- Visual Studio 2026 Community, with the "Desktop development with C++" workload, and its MSVC ARM64 build tools to build for ARM64: https://visualstudio.microsoft.com/vs/

The game runs on Windows 10 or later, on x64 or ARM64.

# Building

FrontierOutpost builds with MSBuild from `FrontierOutpost.slnx`, at the root of this repository. Its build is described in `FrontierOutpost/MIGRATION_NOTES.md`.

## Compiling

Open a **Developer PowerShell for Visual Studio 2026** at the root of the repository, and run

- `msbuild FrontierOutpost.slnx /m /p:Configuration=Release /p:Platform=x64`

Use `Configuration=Debug` for a debug build, and `Platform=ARM64` to build for ARM64. Opening `FrontierOutpost.slnx` in Visual Studio 2026 works too. The binaries land in `FrontierOutpost/bin/<Platform>/<Configuration>/`.

## Getting the Assets

Most of the art, fonts and sounds under `FrontierOutpost/resource/` are Git LFS objects upstream, and this repository holds only their pointer files. To run LT, replace them with the real files from a clone of the original, made with Git LFS installed (`git lfs install`, then `git clone https://github.com/JoshParnell/ltheory-old.git`), and convert its Ogg sounds to WAV. `FrontierOutpost/MIGRATION_NOTES.md` says which files, and which to leave alone (O5 and O10).

## Running an LTSL App

If the compilation is successful, you now have `launch.exe`, which is the main executable. This program launches an LTSL script. The intention was for Limit Theory (and all mods) to be broken into many LTSL scripts, which would then implement the gameplay, using script functions exposed by the underlying engine.

Start it from the `FrontierOutpost` directory, which holds `resource/`, with the script's name:

- `cd FrontierOutpost`
- `bin\x64\Release\launch.exe <script_name_without_extension>`

All top-level scripts are in the `resource/script/App` directory. So you can do, for example:

- `bin\x64\Release\launch.exe war`

To run the app 'war.lts', which is an AI skirmish test. Many of the apps are broken or incomplete, but some work enough to allow you to fly around in a system.

# Example of the Entire Process

An example of the entire sequence of commands to build and run an LTSL app, in a Developer PowerShell for Visual Studio 2026 at the root of this repository, once the assets are in place:

```
msbuild FrontierOutpost.slnx /m /p:Configuration=Release /p:Platform=x64
cd FrontierOutpost
bin\x64\Release\launch.exe war
```
