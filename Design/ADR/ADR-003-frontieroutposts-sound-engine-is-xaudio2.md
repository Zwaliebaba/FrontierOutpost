# ADR-003: FrontierOutpost's sound engine is XAudio2

- **Status:** Accepted (owner, 2026-09-25, after the migration's first x64 build). Amended
  (owner, 2026-09-25): a missing sound file plays silence, by N6 and N9 of
  `Design/Plan/NeuronClient-migration.md`.
- **Scope:** `FrontierOutpost/`
- **Detail:** `FrontierOutpost/MIGRATION_NOTES.md` D15–D18, §11, §12 (BR3, BR9) and §17. This
  supersedes ADR-002's FMOD Ex row.

## Context

The original plays all its sound through FMOD Ex 4.44, in one file,
`src/liblt/Module/SoundEngine/Fmod.cpp`, behind liblt's `SoundEngine` interface:

- 3D sounds on game objects: weapons, shields, impacts, drone bays and the loops of labs and
  transfer units, with the camera as listener and a Doppler shift;
- 2D interface sounds, and the `Sound_*` functions of the LTSL script API;
- decoding: the sounds are 79 Ogg Vorbis and 51 WAV files, which FMOD reads into memory;
- an adaptive music engine, `MusicEngine.cpp`, which drives an FMOD Designer project
  (`resource/music/*.fev` and `*.fsb`) through FMOD's Event System. Nothing creates it: its one
  caller is commented out, in a program the build does not include.

FMOD Ex was discontinued in 2014 and is proprietary; the tree carries no licence text for it. The
original has x86 binaries only, and x64 would have needed files the owner supplies (MIGRATION_NOTES.md
D8). **FMOD Ex has never been built for ARM64**, so ARM64 could not link `lt` at all (D9).

XAudio2 2.9 and X3DAudio are part of Windows 10 and later, and of the Windows SDK, for x64 and
ARM64 alike. They mix and spatialise, but they decode no Ogg Vorbis, and nothing in them
corresponds to FMOD's Event System or Designer projects.

## Decision

1. **`src/liblt/Module/SoundEngine/XAudio2.cpp` implements `SoundEngine` with XAudio2 and
   X3DAudio, on x64 and ARM64.** `launch` starts it (`SoundEngine_XAudio2()`). It reproduces what
   `Fmod.cpp` configured:
   - positions relative to the camera, and FMOD's inverse rolloff from `50 × distanceDiv`, up to
     a distance of 100,000;
   - FMOD's speed of sound for the Doppler shift;
   - FMOD Ex's pan law for 2D sounds, and `SetPitch` as 44,100 Hz times the pitch;
   - every failed call ends the program through liblt's assertion handler.
2. **Sounds are WAV files.** The owner converts the Ogg files to WAV: 24 of them, since the
   NeuronClient plan's Phase 1 removed the 55 that nothing names (ADR-013). Code and scripts name
   the WAV files. The two Ogg files whose WAV name is already taken convert to `<name>_ogg.wav`.
   - **A missing file plays silence** (amendment, N6). The log names it once, as a warning, and
     every sound played from it is created finished. So the conversion can land whenever it is
     ready.
   - **A file that is there but that XAudio2 cannot play ends the program**, through the
     assertion handler (N9). It is a broken asset, not a missing one.
3. **All FMOD material is deleted:** `Fmod.cpp` and `Fmod.h`, `MusicEngine.cpp`, `MusicEngine.h`,
   `MusicEngine/LtheoryTest01.h`, `include/FMOD`, and `resource/music`.
4. **No new dependency.** XAudio2 is linked through the Windows SDK's `xaudio2.lib`, and needs
   nothing else at run time.

## What this forecloses

- FMOD, its Event System, and the Designer project: the adaptive music engine is gone, not ported.
- Ogg Vorbis sounds. Playing one again means a decoder, which is a new dependency (AGENTS.md R14).
- Sound that matches the Win32 original's bit for bit. The engine reproduces FMOD's settings,
  not its mixer. MIGRATION_NOTES.md BR3 lists what may differ.
- Windows before 10, where there is no XAudio2 2.9.
