# ADR-001 — Sixteen colors reach the screen through an index target, not a quantizer

**Status:** Accepted

**Date:** 2026-09-09
**Decided by:** Build session MVP-01, step 2. Recommendation stated in `Design/Plans/MVP-01-IsometricShip.md` §3; this ADR takes it and records why.
**Supersedes:** —

---

## Context

`Design/README.md` §1 fixes the presentation: a 640×400 framebuffer of 16 colors, scaled to the
window by a whole number, on Direct3D 12. The palette is the EGA default 16, settled by owner
decision on 2026-09-09 and listed in `Design/Plans/MVP-01-IsometricShip.md` §2.

That says what the screen is. It does not say where in the pipeline a color stops being a
continuous value and becomes one of sixteen, and there are two genuinely different places to put
that boundary. The choice decides what the edges in the picture look like, because everything a
GPU does to make edges look smooth — multisampling, blending, bilinear filtering, texture
interpolation — is a way of producing a color *between* two colors, and this screen has no
between.

The other constraint that binds is R13: the executable ships alone, so whatever holds the palette
is embedded in the binary rather than loaded. Sixty-four bytes is small enough that this is not a
real limit, but it does rule out treating the palette as data that arrives at runtime.

## Options considered

### A. A paletted render target: every pass writes an index, one resolve pass looks it up

The game draws into a 640×400 `R8_UINT` render target with a matching `D32_FLOAT` depth buffer.
One byte per texel, and the byte *is* the palette index. Nothing in any pass ever computes a
color. At the end of the frame a single fullscreen pass reads the index target with `Load()`,
looks the index up in the 16-entry palette, and writes the 1280×800 back buffer.

The cost is that the whole renderer has to be written in terms of indices. A shader cannot
"darken" anything; it picks a different index. Blending is not available, because a blend of two
indices is an index that means an unrelated color — index 3 blended halfway with index 7 is 5,
which is magenta. Anti-aliasing is not available for the same reason. Text, meshes and UI all
have to decide their index before the rasterizer sees them.

That cost is mostly a description of the target rather than a penalty. A 16-color screen does not
want blending; the effects it would buy are exactly the ones that make it stop looking like a
16-color screen.

### B. Render in RGBA, quantize to the nearest palette entry at the end

The game draws in `R8G8B8A8_UNORM` the way any modern renderer would, and the resolve pass
searches the palette for the nearest entry to each pixel and writes that.

This is the cheaper option to write. Every existing D3D12 technique keeps working, shaders compute
colors normally, and the paletting is one function at the end. It also degrades gracefully: if the
palette changed, nothing but the resolve pass would care.

It loses on edges, and the loss is not subtle. By the time the quantizer runs, the rasterizer has
already produced the interpolated pixels along every triangle edge and every glyph. Quantizing
them does not remove them; it snaps each one to whichever of sixteen colors is nearest, which
turns a two-pixel gradient into a two-pixel band of some third color. A hull edge between index 1
and index 9 acquires a fringe of index 8. That fringe is what a 16-color screen never had, and it
is visible at 2×, where every virtual pixel is a 2×2 block.

It is also the wrong place for the decision in a more structural sense. Under B the palette is a
post-process the renderer is unaware of; under A the palette is the renderer's type system. The
day somebody adds a pass that blends, A fails at compile time and B produces a slightly wrong
picture nobody notices.

## Decision

Sixteen colors reach the screen through a **640×400 `R8_UINT` index target** with a matching
`D32_FLOAT` depth buffer. Every pass the game draws writes a palette index and never a color. One
fullscreen resolve pass, `PaletteResolveVS`/`PaletteResolvePS`, reads the index target and writes
the back buffer through the palette.

The resolve shader reads the index target with `Texture2D<uint>::Load()` and there is **no sampler
object anywhere in the pass**. Point sampling is therefore not a setting that could be changed but
a property of the code: `Load` takes integer texel coordinates and has no filtering to switch on.
The blow-up from 640×400 to 1280×800 is an integer divide of `SV_Position` by the present scale,
passed as a root constant.

The palette lives in `NeuronClient/Palette.h` as a `constexpr std::array<std::uint32_t, 16>` of
`0x00RRGGBB` values and is bound as 16 root constants — 17 DWORDs of the root signature's 64,
counting the scale. No upload heap, no constant buffer, nothing to keep alive across frames.

The back buffer is `R8G8B8A8_UNORM` and deliberately **not** `_SRGB`, so the bytes the resolve
writes are the bytes the swap chain presents.

## Consequences

**What this makes easy.** Correctness is checkable rather than a matter of opinion. A screenshot
of a screen cleared to index 1 reads `0000AA` exactly — verified on 2026-09-09 by sampling the
captured 1280×800 client area at four points, all `0000AA`. Any pixel that is not one of the
sixteen is a bug with a short list of possible causes. The palette is pinned value-by-value by a
test in `NeuronClientTests`.

**What this makes hard, on purpose.** No blending, no multisampling, no anti-aliased lines, no
filtered textures, no post-process that works in color space. `NeuronClient/D3D12Defaults.h`
disables all of them in the shared pipeline defaults, so a new pass gets the restriction without
having to remember it. Anything that wants a gradient has to dither or pick a different index —
and ADR-002 decides what the mesh pass does about that.

**What it costs.** One extra fullscreen pass per frame and one extra 640×400 byte-per-texel
surface (256 KB) plus its depth buffer. Both are negligible at this resolution; neither was
measured, because there is no plausible arithmetic in which 256 KB and one fullscreen triangle
matter on hardware that runs D3D12.

**What it forecloses.** A future where the game wants more than 16 colors on screen at once is a
new ADR and a new resolve pass, not a setting. That is intended: `Design/README.md` §1 makes 16
colors a design constraint rather than a preference.

## What this changes elsewhere

- **Code:** `NeuronClient/PaletteTarget.{h,cpp}`, `NeuronClient/Palette.h`,
  `NeuronClient/Shaders/PaletteResolve{VS,PS}.hlsl` and `NeuronClient/D3D12Defaults.h` implement
  this and cite it. `FrontierOutpost.cpp` draws everything between `BeginScene()` and `Resolve()`.
- **AGENTS.md:** no change. R12 already fixes the screen; this ADR says how it is built.
- **Design/:** no document superseded.

## Open questions

Whether the depth buffer needs a bias to keep the hull's silhouette clean at 2:1 is left to
ADR-003 and the mesh pass, per `Design/Plans/MVP-01-IsometricShip.md` §6.

Nothing here decides how a *lit* surface picks its index; a face that is partly in shadow is not a
question the resolve pass can answer. That is ADR-002.
