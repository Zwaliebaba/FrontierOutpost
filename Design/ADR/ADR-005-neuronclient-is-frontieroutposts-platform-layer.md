# ADR-005: NeuronClient is FrontierOutpost's platform layer

- **Status:** Accepted (owner, 2026-09-25, at the start of the NeuronClient migration's Phase 2)
- **Scope:** `NeuronClient/` and `Tests/NeuronClientTests/`, two new projects at the repository
  root, and the boundary between them and `lt.dll`
- **Detail:** `Design/Plan/NeuronClient-migration.md` §1, §3 (point 5), §4.1, §5.1, §5.2, §5.4,
  §10, N0, N10 and N12. Plan §8 lists how ADR-001 to ADR-004 change with this ADR and the eight
  after it.

## Context

liblt reaches the platform through four routes (plan §4.1):

- SFML 2.5 for the window and its events, keyboard and mouse polling, joysticks, threads and
  clocks, image load and save, and one HTTP client;
- GLEW 1.7 and OpenGL 2.1 for all rendering;
- FreeType 2.5.5 for glyph coverage and kerning;
- XAudio2 and X3DAudio for all sound (ADR-003).

The owner's brief (N0) moves this onto **NeuronClient**, a new library written for this engine,
except threads and clocks, which move to the C++ standard library (plan §1). Outpost.Commander's
NeuronClient is a reference for conventions, not code to copy: it is UWP and CoreWindow, touch
only, has no audio and loads only system fonts. Five repositories already carry copies of a
NeuronClient, and they have drifted apart, from 45 to 139 files (plan §3, point 5).

The boundary has one hard constraint (plan §5.1). liblt defines `near`, `far`, `DrawState`,
`interface` and `GetObject`, which Windows headers define as macros, and its include order is
load-bearing (`FrontierOutpost/MIGRATION_NOTES.md` H3).

## Decision

1. **NeuronClient is a static library, and liblt reaches graphics, glyphs, images, sound, the
   window and input only through it** (plan §1, §5.2):
   - Direct3D 12 and DXGI for graphics (ADR-007);
   - DirectWrite for glyphs (ADR-010);
   - WIC for images (ADR-011);
   - XAudio2 and X3DAudio for sound: ADR-003's mechanism moves in, and liblt keeps an adapter;
   - user32 for the window and input (ADR-012).
2. **What it leaves out** (plan §5.2): threads, mutexes and clocks, which liblt takes from the
   standard library directly; networking, since nothing is left that uses it; joysticks, which are
   removed; and liblt's own calls for OS services, which stay where they are (N12):
   `SHGetFolderPath`, `GetModuleFileNameA`, `CreateDirectoryA`, `MessageBoxA` and DbgHelp in
   `LTE/OS.cpp`, `MessageBoxA` in `Common.cpp`'s assertion handler, and `WinMain` in `LTE/LTE.h`.
3. **The layers run one way** (plan §5.1): `launch.exe`, then `lt.dll`, then `NeuronClient.lib`,
   then the Windows SDK. Of the game's binaries, only `lt.dll` links NeuronClient, statically;
   `launch.exe` never sees it. It uses `/MD` and `/MDd`, and `lt`'s `/arch`, to match `lt`
   (ADR-006).
4. **NeuronClient knows nothing of the game** (AGENTS.md R9). It has no `String`, `V3`, `Object`
   or `Key_*`: only C++ and its own small types. liblt adapts to it. A table maps `Neuron::Key` to
   the engine's `Key`, and the sound adapter keeps carriers, the camera as listener and the
   `distanceDiv` mapping (plan §5.4).
5. **No public header includes a Windows, DXGI or Direct3D header.** Each class keeps its platform
   objects in a private implementation, as Outpost.Commander's NeuronClient does (plan §5.1).
6. **Its conventions** (plan §5.1):
   - namespace `Neuron`;
   - Unicode inside, UTF-8 at the API;
   - COM objects in `Microsoft::WRL::ComPtr` (AGENTS.md R12);
   - no NuGet package, and no `d3dx12.h`, which is not in the SDK (AGENTS.md R14).
7. **AGENTS.md governs NeuronClient and its tests in full,** amended for ARM64 alone (ADR-006).
   ADR-001's exemption covers the migrated code, and new code outside `FrontierOutpost/` was never
   in it; liblt's new `Shaders/` folder leaves it too (N11, ADR-008). Inside `lt.dll`,
   NeuronClient builds at `/W4 /WX /fp:precise` and liblt at `/W3 /fp:fast`. Warning levels do
   not cross the link, and the CRT must match. A template or inline function both use keeps one
   copy, which the linker picks: that is why NeuronClient states `lt`'s `/arch`, and why such a
   copy may round as either library's `/fp` would, which nothing relies on (ADR-006, plan §10).
8. **It is written game-agnostic, so it could become the shared NeuronClient.** This migration
   does not extract it (plan §3 point 5, §11).

## What this forecloses

- **Copying Outpost.Commander's NeuronClient,** or any of the other copies, into this tree (N0).
- **Game concepts in NeuronClient.** Code that has to name one belongs in liblt (AGENTS.md R9).
- **A second route from liblt to graphics, glyphs, images, sound, the window or input.** SFML,
  GLEW, OpenGL and FreeType leave the tree (plan §1).
- **Moving liblt's calls for OS services into NeuronClient** in this migration (N12).
- **NeuronClient as a DLL, or linked into `launch.exe`.**
- **Extracting NeuronClient into a repository of its own** as part of this migration (plan §11).
