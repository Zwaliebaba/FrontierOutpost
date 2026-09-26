# ADR-010: Glyphs are rasterised by DirectWrite, from the font files in GameData

- **Status:** Accepted (owner, 2026-09-25, at the start of the NeuronClient migration's Phase 2)
- **Scope:** NeuronClient's `FontFace`, text in `lt.dll`
  (`FrontierOutpost/src/liblt/LTE/Font.cpp`), and `GameData/font`
- **Detail:** `Design/Archive/NeuronClient-migration.md` §4.1, §4.3, §5.2 (`FontFace`), §5.4
  (`Font.cpp`), §9 D, §11, Phase 2 step 4, N0 and N3

## Context

liblt draws text from a distance field (plan §4.1, §4.3):

- FreeType renders each glyph at 64 px into an R16F coverage atlas, and kerning comes from the
  font's `kern` table;
- a GPU pass (`sdffont.jsl`, 129² fetches per texel) turns the atlas into an R32F distance field,
  whose mipmaps are regenerated after every glyph;
- text then draws at any size from the distance field.

FreeType 2.5.5 is vendored source (ADR-002), and in liblt only `LTE/Font.cpp` calls it. It opens
the game's own font files, from `GameData/font/` (ADR-004). Four families are used: Rajdhani,
Iceland, SourceCodePro and Gafata. Play is used only by the `font` toy app.

N0 replaces FreeType with DirectWrite. Outpost.Commander's NeuronClient, the reference for
conventions, loads only system fonts.

## Decision

1. **`Neuron::FontFace` wraps DirectWrite** (`dwrite`, plan §5.2). It opens a face from a font
   file, looks glyphs up, and renders 8-bit coverage at a pixel size. It gives each glyph's
   advance and bearing, and each pair's adjustment from the `kern` table.
2. **Faces are opened from the files in `GameData/font`,** as FreeType's were, not from the fonts
   installed on the system.
3. **Only the rasteriser changes** (plan §5.4). `FontFace` replaces FreeType in `LTE/Font.cpp`,
   and the distance-field pass stays on the GPU.
4. **Four families stay, with their licences:** Rajdhani, Iceland, SourceCodePro and Gafata. The
   other 34 go, NotoSans, NotoSansCJKsc and Play among them, and so do the `FontPreview` and
   `SplashScreen` widgets that nothing opens (plan §9 D). Phase 1 re-verifies the list (ADR-013).
5. **FreeType leaves the tree:** `FrontierOutpost/ext/freetype` and
   `FrontierOutpost/include/FreeType` go (plan Phase 2 step 4).
6. **Parity with FreeType's glyphs is not the bar** (N3). NeuronClientTests check glyph coverage
   and kerning in CI, and the owner checks text in the kept apps (plan Phase 2).

## What this forecloses

- **FreeType, or any other glyph rasteriser.** Bringing one back is a new dependency (AGENTS.md
  R14).
- **Text in a font the game does not ship.** A family the game uses is a file in `GameData/font`,
  with its licence beside it (ADR-004).
- **The 34 removed families,** unless the owner approves bringing one back, with its licence
  (AGENTS.md R14).
- **Moving the distance field to the CPU** in this migration: that is backlog (plan §11).
