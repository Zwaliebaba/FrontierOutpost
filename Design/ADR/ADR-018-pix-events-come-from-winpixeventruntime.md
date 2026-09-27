# ADR-018: PIX events come from WinPixEventRuntime

- **Status:** Accepted (owner, 2026-09-27, for the shader performance plan's M2)
- **Scope:** `NeuronClient/packages.config`, `NeuronClient/NeuronClient.vcxproj`,
  `DrawContext::BeginEvent` and `EndEvent`, the regions `RenderPass.cpp` and `Scheduler.cpp` open,
  `Build/CheckProjectFiles.py`'s `APPROVED_PACKAGES`, and the restore step in
  `.github/workflows/build.yml`
- **Amends:** ADR-007 decision 2 ("PIX markers without WinPixEventRuntime")
- **Amended:** 2026-09-27 by ADR-019. Decision 1's "one NuGet package" has C++/WinRT beside it
  now, in every project. Decision 2 still holds for WinPixEventRuntime; ADR-019 says why C++/WinRT's
  settings come from its own build files instead.

## Context

ADR-007 gave `DrawContext` regions for PIX, written in PIX's format by hand so that NeuronClient
needed no runtime (R14). They worked, and a test exercises them, but nothing in the game opened one,
so a capture showed the frame as one flat list of draws. The shader performance plan's M2 needs
per-pass GPU times from a PIX capture, and the owner asked for Microsoft's WinPixEventRuntime NuGet
package (`pix3.h`) to be added and used. It also puts each region on PIX's CPU timeline.

The repository had no package manager: ADR-002, which covered vendored dependencies, is archived
because none was left, and `Build/CheckProjectFiles.py` refused any `packages.config`.

## Decision

1. **WinPixEventRuntime 1.0.240308001** (MIT, the latest on nuget.org on 2026-09-27) is
   NeuronClient's one NuGet package, named in `NeuronClient/packages.config`. It is restored into
   `packages/` at the repository root, which `.gitignore` already leaves out, by
   `msbuild FrontierOutpost.slnx -t:restore -p:RestorePackagesConfig=true`. Visual Studio restores it
   on its own. CI restores it before the build.
2. **Its settings are stated in `NeuronClient.vcxproj`,** not taken from the package's `.targets`,
   so that the checkers read them (AGENTS.md §3): the include path, and the import library, which
   the librarian puts into `NeuronClient.lib` so that the executable and the test suites link it
   with no setting of their own. A target copies `WinPixEventRuntime.dll` into the output folder they
   share, and a build without the restore stops with the command to run.
3. **Events are on in every configuration.** `DrawContext.cpp` defines `USE_PIX` before `pix3.h`,
   which otherwise turns them on in Debug only, because the measurements are taken in Release (the
   plan's §7 question 1). `pix3.h` is included there and in `GpuCapture.cpp` (decision 7), and
   nowhere else.
4. **`DrawContext::BeginEvent` and `EndEvent` keep their contract** (they nest, and stay open across
   command lists), and write each region with `PIXBeginEvent`/`PIXEndEvent` on the command list and
   on the calling thread. The name is an argument to `"%s"`, never the format.
5. **Regions are opened** for each render pass, named as the profiler names it
   (`RenderPass.cpp`), and for each run of a scheduler job, named for the job (`Scheduler.cpp`), which
   covers the load-time generation: fields, levels of detail and occlusion.
6. **`Build/CheckProjectFiles.py` accepts a package only at a version in `APPROVED_PACKAGES`,** with
   the ADR that approved it. A new package or version is a new decision. `PackageReference` stays
   refused.
7. **The program can take a GPU capture of itself** (owner, 2026-09-27, so that M2 needs nobody at
   the keyboard): `FrontierOutpost.exe <app> --frames N --gpu-capture <path>` loads the newest
   `WinPixGpuCapturer.dll` of the PIX installed on the machine before the device is made, asks for
   one frame 60 frames before the last, and waits for PIX to write the `.wpix` file. It fails with a
   message when PIX is not installed. `NeuronClient/GpuCapture.cpp` holds the two calls, and is,
   with `DrawContext.cpp`, the only file that includes `pix3.h`. PIX itself is a development tool on
   the machine, not a dependency: nothing links or ships it.

## Runtime file (R13)

`WinPixEventRuntime.dll`, beside `FrontierOutpost.exe` and the test suites. The executable imports
it, so it is **required to start**: a build that ships the game ships the DLL with it, for x64 or
ARM64 as built. The program never writes it.

## What this forecloses

- **A build with no network on a clean checkout,** until the package is in NuGet's cache or
  `packages/`.
- **An executable that starts without the DLL.** Delay-loading it would move the failure to the first
  event, which is not better.
- **Other NuGet packages without their own ADR,** which the checker enforces.

## Verification

On 2026-09-27, on the owner's machine: the restore command put the package into `packages/`, Debug
and Release built for x64, and the executable lists `WinPixEventRuntime.dll` among its imports, found
beside it. `war` and `hud` ran 60 frames in Debug with the Direct3D 12 debug layer and exited 0, so no
region was left unbalanced. NeuronClientTests' `DebugEvents` passed, with the rest of the 127 tests.

`war --frames 900 --gpu-capture` in Release, with PIX 2603.25 installed and the desktop locked,
wrote a 484 MB capture. `pixtool open-capture … save-event-list` listed its 3,647 events under the
named regions: Camera Pass (Depth Prepass, GBuffer Pass, Global Lighting Pass, Local Lighting,
Blended, Lens Flares, Bloom, tonemap, colour grade), SMAA, Interface and dither, 334 draws in all.
The events' GPU times need PIX's performance-logging permission, which an unelevated account
outside Performance Log Users does not have (`E_PIX_MISSING_PERFORMANCE_LOGGING_PERMISSIONS`).
