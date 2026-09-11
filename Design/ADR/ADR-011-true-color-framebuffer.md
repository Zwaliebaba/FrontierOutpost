# ADR-011 — 1280×720 in R8G8B8A8, drawn straight into the back buffer

**Status:** Accepted

**Date:** 2026-09-10
**Decided by:** Owner decision, 2026-09-10. The owner asked for 1280×720 and an R8G8B8A8 colour format, and chose between the two ways of reading that request (below); this ADR records the choice and what it costs.
**Supersedes:** ADR-001

---

## Context

Until today `Design/README.md` §1 fixed the presentation at **640×400, 16 colours**: a paletted
framebuffer scaled to the window by a whole number. ADR-001 decided how those sixteen colours
reached the screen — every pass wrote an `R8_UINT` palette index into a 640×400 target, and one
fullscreen resolve pass read the index, looked it up in a 16-entry table and wrote the 1280×800
back buffer with an integer divide of `SV_Position` by the present scale.

That design was internally consistent and it did what it was built to do. Its cost, stated in
ADR-001's own consequences, was that the renderer's output was **a name rather than a quantity**:
no blending, no anti-aliasing, no filtered texture, no post-process, and eight materials per scene
(ADR-002). ADR-001 also wrote down what the escape hatch would be — "a future where the game wants
more than 16 colours on screen at once is a new ADR and a new resolve pass, not a setting."

This is that ADR. The owner has asked for 1280×720 and R8G8B8A8, which changes both halves of the
baseline at once: the resolution and the format. The two are not independent, which is why they
are decided together here rather than in two documents. At 640×400 the blow-up factor was what
made a legacy screen visible on a modern display; at 1280×720 there is nothing to blow up, because
1280×720 is already a size a modern display shows at 1:1.

The constraints that still bind are unchanged. R13: the executable ships alone, so whatever holds
a colour is embedded in the binary. R12: Direct3D 12 only. R14: the Windows SDK and nothing else.

## Options considered

### A. Keep the index target, at 1280×720

Every pass still writes an `R8_UINT` index; the resolve pass still looks it up and still writes an
`R8G8B8A8_UNORM` back buffer. Only the resolution changes.

This is the smallest possible change, and it is a defensible reading of the request — the back
buffer already *was* `R8G8B8A8_UNORM` before today, so "change the format to R8G8B8A8" would be
satisfied by doing nothing at all to the format. That is also what is wrong with it. It keeps
every restriction ADR-001 imposed, keeps the sixteen-colour limit that the request was plainly
asking to lift, and keeps a fullscreen pass whose remaining job at a present scale of one is to
copy a picture onto a picture of the same size.

### B. Remove the index target and draw colour directly

Every pass writes a `float4` colour into an `R8G8B8A8_UNORM` render target. The `R8_UINT` target,
the palette lookup, the resolve pass and its two shaders all go.

The back buffer *is* that render target. There is no intermediate surface, because at 1280×720
presented 1:1 there is nothing an intermediate surface would buy: it would be the same pixels in
the same format at the same resolution, copied once more.

What this costs is the checkability ADR-001 was built for. Under A, "any pixel that is not one of
the sixteen is a bug" is a statement a test can make about a screenshot. Under B it is not — the
target can hold 2^24 colours, so a wrong colour is only wrong if somebody knows what it should have
been. Some of that is recovered by keeping the colours *named* (§Decision) and by ADR-012 keeping
the mesh pass down to two tones a face, but it is genuinely weaker, and it is the real price here.

It also gives up the structural guarantee. ADR-001's best argument was that a pass which blends
under scheme A **fails**, where under a colour scheme it produces a slightly wrong picture nobody
notices. That argument was correct and it no longer applies. What replaces it is a convention:
`D3D12Defaults.h` still disables blending and multisampling in the shared pipeline defaults, so a
pass gets the restriction by default — but it is now a default a pass can override rather than an
impossibility, and the guard against that is review and an ADR rather than the type system.

### C. Draw colour into an offscreen target, then blit to the back buffer

Between A and B: colour everywhere, but the game still renders into its own `R8G8B8A8_UNORM`
surface which a final pass copies to the back buffer.

This is what you want the day the presentation stops being 1:1 — a different window size, a
fullscreen mode with letterboxing, or a post-process that needs to read the finished frame. None
of those exist. Today it is one more surface (3.5 MB), one more fullscreen pass and one more
format to keep in agreement, buying an indirection nothing uses. Rejected as machinery ahead of
its case (R15's spirit), and it is a change that can be made later without disturbing anything
else here: it is exactly the resolve pass this ADR is deleting, minus the palette.

## Decision

The game draws **1280×720 `R8G8B8A8_UNORM`, directly into the swap chain's back buffer**, with a
matching 1280×720 `D32_FLOAT` depth buffer. The render target, the back buffer and the window's
client area are the same 1280×720 pixels; **the present scale is gone, not set to one.**

The back buffer is `R8G8B8A8_UNORM` and deliberately **not** `_SRGB`, which is unchanged from
ADR-001 and for the same reason: a channel authored as `0xAA` is presented as `0xAA`, so a
screenshot can be compared against the source byte for byte.

`NeuronClient/PaletteTarget` becomes `NeuronClient/SceneTarget` and keeps only the part of its job
that survives — owning the depth buffer, and being the one place that says what a frame opens
with. `NeuronClient/Palette.h` becomes `NeuronClient/Color.h`.

**The colours stay named.** `Color.h` holds a `struct Color` of four `std::uint8_t` channels and
the same sixteen names the palette had, at the same values — the EGA default 16, which is what
every mesh in the tree was drawn against. What is gone is the *limit*: a seventeenth colour is a
line in that file rather than a new ADR. Keeping the names is what stops this change from turning
a tree where materials are called `HULL_COLOR` into one where they are called `0xFFAAAAAA`.

`Pack()` in `Color.h` is the single statement of channel order, and it puts **red in the low byte**
to match `DXGI_FORMAT_R8G8B8A8_UNORM`, not the `0x00RRGGBB` a person types by hand. Every vertex
colour in the renderer is an `R8G8B8A8_UNORM` input element, so the shader receives a `float4`
already divided by 255 and neither side does any unpacking arithmetic.

**Point sampling survives, and still not as a setting.** There is no sampler object anywhere in
this renderer. The font atlas is read with `Texture2D<uint>::Load()`, which takes integer texel
coordinates and has no filtering to switch on; the starfield is a hash of an integer pixel; the
meshes have no textures. That was ADR-001's argument for `Load()` and it did not depend on the
palette.

## Consequences

**What this makes easy.** Anything that needs a colour that is not one of sixteen. A gradient, a
second shade of gray, a UI element that is not one of the EGA hues, a fade — none of these are
now impossible, which is the whole point of the request. Adding a colour is adding a name.

The render path is also shorter by a pass and a surface: 256 KB of index target and one fullscreen
triangle a frame are gone, and the frame is four calls rather than five. Neither was measured
before or after, and neither is claimed as a performance result — at this scale they were
negligible in both directions. What is worth stating is that the *code* is shorter: two shaders,
one class's worth of resolve machinery, and the present-scale argument threaded through `Device`,
`PaletteTarget` and `PointerInput` all went.

**What this makes hard, and what it stops making hard.** ADR-001's restrictions are now
conventions. `D3D12Defaults.h` still turns blending, multisampling and anti-aliased lines off for
every pipeline built from the shared defaults, and the comments there say why — but a pass can now
set its own `D3D12_BLEND_DESC`, and the thing standing between this renderer and an accidental
half-transparent sprite is review rather than a compile error. **A pass that wants blending or
multisampling is an ADR.**

**What it costs.** The screenshot test gets weaker, as above. And the picture is no longer
self-describing: "six colours on screen, indices 0, 4, 7, 9, 12 and 15" was a complete description
of a frame under ADR-002, and the equivalent statement now has to name RGB triples.

Measured on 2026-09-10, from a capture of the running client area at 1280×720: 8 colours cover the
frame — `000000` (877,817 px), `AAAAAA` (17,006), `FFFFFF` (13,176), `5555FF` (8,625), `FF5555`
(3,038), `55FF55` (1,404), `AA0000` (423), `555555` (57). Every one of them is a name in
`Color.h`. The remaining 54 pixels of the 921,600 are Windows 11's rounded window corners
compositing over the bottom two corners of the client area, at (x ≤ 3 or x ≥ 1276, y ≥ 711); they
are the desktop, not the game.

**What it forecloses.** Nothing that was wanted. The paletted look is reachable again by writing
one, at some cost, but it would be a new ADR and a new pass — this document does not pretend the
decision is cheaply reversible.

## What this changes elsewhere

- **Code:** `NeuronClient/Color.h` (was `Palette.h`) and `NeuronClient/SceneTarget.{h,cpp}` (was
  `PaletteTarget.{h,cpp}`) are new. `NeuronClient/Shaders/PaletteResolve{VS,PS}.hlsl` are deleted.
  `Mesh{VS,PS}`, `Text{VS,PS}` and `StarfieldPS` write colour; `MeshRenderer`, `FontRenderer`,
  `Starfield` and `Device` name `R8G8B8A8_UNORM`; `PointerInput::Create` lost its present-scale
  argument; `Lockstep.cpp` no longer resolves.
- **AGENTS.md:** R12 rewritten, §2's repository map and §1's worked example updated. Done in the
  same commit.
- **Design/:** `README.md` §1's Presentation row rewritten. ADR-001 superseded. ADR-002 superseded
  by ADR-012, which this ADR forces. ADR-013 carries the pixel-denominated consequences.
- **Design/Archive/MVP-01-IsometricShip.md** describes the 640×400 renderer in the past tense and
  is deliberately **not** edited: it is the record of what was built and believed then.

## Open questions

Whether the game should ever present at a size other than 1280×720 — a resizable window, a
fullscreen mode, a second aspect ratio. Option C above is the shape that answers it and it is
written down so the next session does not have to re-derive it.

Nothing here decides how a lit surface picks its colour now that it has more than two to choose
from. That is ADR-012, and the answer is that it still picks between exactly two.

Nothing here decides what the UI looks like at 1280×720. The owner is writing that separately;
`FontRenderer::GLYPH_SCALE` (ADR-013) is the one knob the renderer currently offers it.
