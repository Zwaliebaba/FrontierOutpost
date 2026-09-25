# ADR-011: Images are decoded and encoded by WIC

- **Status:** Proposed (2026-09-25, NeuronClient migration Phase 0), for the owner to accept
- **Scope:** NeuronClient's `ImageFile`, and how `lt.dll` loads and saves images
- **Detail:** `Design/Plan/NeuronClient-migration.md` §4.1, §5.2 (`ImageFile`), §5.4, §5.5, §7,
  §8 (runtime files), §9, Phase 2 step 3 and Phase 4 step 7

## Context

liblt loads and saves images through SFML's `sf::Image` (plan §4.1):

- `Texture_LoadFrom` decodes a file into an RGBA8 texture, top row first, as SFML's loader
  returns it (`FrontierOutpost/ext/SFML/src/SFML/Graphics/ImageLoader.cpp`);
- `Texture2D::SaveTo` writes a texture, and the F1 screenshot goes to `cache/screenshot/` as a
  PNG (ADR-004);
- `CubeMap::SaveTo` writes six PNGs, but nothing calls it (plan §9 B).

The images in `GameData/texture` are JPEG and PNG. The one other format the tree writes is BMP, in
the `font` toy app (`GameData/script/App/font.lts:17`), which goes (plan §9 A). `sf::Image` and
`sf::RenderWindow` are what keep `sfml-graphics` in the link (plan Phase 2 step 3).

## Decision

1. **`Neuron::ImageFile` wraps WIC** (`windowscodecs`, plan §5.2). It decodes PNG and JPEG to
   RGBA8, top row first, and encodes PNG.
2. **liblt loads and saves every image through it** (plan §5.4): textures, screenshots, and the
   PNG captures of the CI smoke mode (plan Phase 4 step 7).
3. **The row order liblt sees does not change.** The renderer keeps OpenGL's memory layout, and
   readbacks keep liblt's existing CPU flips (ADR-007, plan §5.5).
4. **`sfml-graphics` leaves the link** once `ImageFile` replaces `sf::Image` and `Window.cpp` and
   `Mouse.cpp` move from `sf::RenderWindow` to `sf::Window` (plan Phase 2 step 3).
5. **No runtime file is added for players** (AGENTS.md R13, plan §8). Screenshots stay under
   `cache/screenshot/`. The CI smoke mode writes its PNG captures only where `--capture` says, and
   a relative path resolves against the folder `launch.exe` works from, the one that holds
   `GameData/` (ADR-004).
6. **A WIC round trip is part of NeuronClientTests,** run in CI (plan Phase 2, §7).

## What this forecloses

- **Loading anything but PNG and JPEG, or saving anything but PNG.**
- **A bundled image library.** WIC ships with Windows, so images add no dependency (AGENTS.md
  R14).
- **SFML for images,** and `sfml-graphics` in the link.
