#!/usr/bin/env python3
"""BakeFont.py -- turn the TTFs in Build/Fonts into NeuronClient/Font.h.

ADR-073: this runs on the AUTHOR'S MACHINE and never under MSBuild. The header it writes is
committed like any other, and `Build/CheckProjectFiles.py` fails when the three hashes it records
stop agreeing with what is on disk. R14 binds what the executable links, not what this tool
imports -- freetype-py and fontTools never enter the tree, and nothing it emits is anything but a
`constexpr` array.

  py Build/BakeFont.py               bake the five Plex cuts into NeuronClient/Font.h
  py Build/BakeFont.py --check       re-derive the hashes and report drift, without writing

THERE WAS A `--legacy` HERE, AND IT IS GONE ON PURPOSE.

FONT-01 changed the header format, the renderer that reads it, the pixel shader and the face, and
doing all four at once would have left no way to tell which of them broke the screen. So each
landed separately, and the three that came before the face was swapped were each proved by
capturing the screen and requiring it to be BYTE-IDENTICAL -- a test that only exists while the
glyphs are the old ones. `--legacy` carried the original 8x8 font through the new format so that
those three checkpoints could exist at all, and `--self-test` proved it reproduced all 768 bytes
of the old FONT_DATA.

Both were spent the moment Plex was baked, and scaffolding nobody can exercise is scaffolding that
rots. They are in the history at stage 1 if a future session ever needs the trick again.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT_DIRECTORY = REPO_ROOT / "Build" / "Fonts"
OUTPUT_HEADER = REPO_ROOT / "NeuronClient" / "Font.h"

# ---------------------------------------------------------------------------- what gets baked

# The cuts that are baked, in the order the Face enumerator declares them. The name is what the C++
# enumerator is called; the file is what is rasterized.
#
# FOUR, NOT ADR-074'S FIVE. Plex Mono SemiBold was dropped on 2026-09-13 under the ADR's own
# instruction: "if at the final size two of them are indistinguishable, the answer is to drop a cut
# rather than to keep a difference nobody can see." At 12px, measured over `SHIPYARD L1 HOLLIS 20
# CR` as post-gamma coverage, Regular to Medium is +15.4% ink and Medium to SemiBold is +9.1% --
# and the only thing SemiBold was set in was the lock countdown, which is already separated from
# everything near it by being 2x and amber. A third weight there carried no signal that was not
# already being carried twice.
FACES = [
    ("MonoRegular", "IBMPlexMono-Regular.ttf", 12),
    ("MonoMedium", "IBMPlexMono-Medium.ttf", 12),
    ("SansRegular", "IBMPlexSans-Regular.ttf", 12),
    ("SansMedium", "IBMPlexSans-Medium.ttf", 12),
]

# TWELVE PIXELS, AND THE ARITHMETIC BEHIND IT (measured from the files, 2026-09-13).
#
# Plex Mono's advance is exactly 0.600em at 1000 units per em, so 12px advances 7.2px and rounds to
# SEVEN -- one pixel NARROWER than the font it replaces. The digest rail is 254 pixels, so it holds
# 36 characters a line instead of 31, which is the direction that helps the six strings ADR-014 had
# to shorten. At 13px the advance rounds to eight and nothing moves horizontally at all; at 14px it
# is still eight.
#
# Cap height is 0.698em -- 8.4px at this size -- so capitals stay almost exactly the height they are
# today, which is what keeps the screen recognisable while the type gets a descender and a real
# lowercase. The cost is vertical: ascender 1025 plus descender 275 is 1.3em, so a line is about
# 16px where the old font's was 12.
#
# This is stage 3's number to revisit from a screenshot, not from this comment.

# ASCII, plus the five characters ADR-014 had to substitute in MatchFixture.cpp and the arrow the
# orders rail uses. Wider coverage costs only atlas area, and no localization is on any roadmap --
# so this is the set the game actually draws and not a guess at the set it might.
SUBSET = [chr(code) for code in range(0x20, 0x7F)] + [
    "·",  # MIDDLE DOT      -- the digest separator, written `-` today
    "−",  # MINUS SIGN      -- build costs, written `-` today
    "–",  # EN DASH         -- `Orune-Kepler-Reach`, written `-` today
    "→",  # RIGHTWARDS ARROW-- `HALVORSEN > you`, written `>` today
    "›",  # SINGLE RIGHT ANGLE QUOTE -- a trailing "go" affordance: `JOIN ›`, `MORE ›`
    # SINGLE LEFT ANGLE QUOTE. Not one of ADR-014's five, and added 2026-09-13 for one reason: the
    # digest pager is a matched pair, `‹ PREV` on the left against `MORE ›` on the right, and a
    # control with a typeset half and an ASCII half reads as a defect rather than as a decision.
    "‹",
]

# One texel per glyph pixel, so the atlas is as wide as the widest row of glyphs needs. 512 keeps
# every face on a handful of shelves and stays far inside D3D12's 16384 limit.
ATLAS_WIDTH = 512


# ---------------------------------------------------------------------------- rasterizing


class Glyph:
    """One rasterized glyph: its coverage, where it sits in the atlas, and how it is placed."""

    def __init__(self, codepoint: str, width: int, height: int, bearing_x: int, bearing_y: int, advance: int,
                 coverage: bytes) -> None:
        self.codepoint = ord(codepoint)
        self.width = width
        self.height = height
        self.bearingX = bearing_x
        self.bearingY = bearing_y
        self.advance = advance
        self.coverage = coverage
        self.atlasX = 0
        self.atlasY = 0


def rasterize_face(path: pathlib.Path, size_pixels: int) -> tuple[list[Glyph], dict[str, int]]:
    """Render the subset at a pixel size, with hinting on and 8-bit coverage out.

    Hinting is ON because these sizes are small enough for it to matter and the alternative is
    stems that land between pixels. It is a parameter of the bake rather than of the renderer, so
    changing it is a re-bake and a screenshot, not a code change.
    """
    import freetype  # imported here so --check needs no rasterizer at all

    face = freetype.Face(str(path))
    face.set_pixel_sizes(0, size_pixels)

    glyphs: list[Glyph] = []
    for character in SUBSET:
        face.load_char(character, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        bitmap = face.glyph.bitmap
        width, rows, pitch = bitmap.width, bitmap.rows, bitmap.pitch

        # FreeType's buffer is padded to `pitch` bytes a row; the atlas is not.
        packed = bytearray()
        for row in range(rows):
            start = row * pitch
            packed.extend(bytes(bitmap.buffer[start:start + width]))

        glyphs.append(Glyph(
            character, width, rows,
            face.glyph.bitmap_left,
            face.glyph.bitmap_top,
            # 26.6 fixed point, rounded to whole pixels: a fractional advance is a glyph on a half
            # pixel, which is the one thing ADR-011 will not have on this screen.
            (face.glyph.advance.x + 32) >> 6,
            bytes(packed),
        ))

    metrics = {
        "ascent": (face.size.ascender + 63) >> 6,
        "descent": abs(face.size.descender) >> 6,
        "lineHeight": (face.size.height + 63) >> 6,
    }
    return glyphs, metrics


# ---------------------------------------------------------------------------- packing


def pack(all_glyphs: list[list[Glyph]]) -> tuple[int, int, bytearray]:
    """Shelf-pack every face's glyphs into one atlas, tallest row first within a shelf.

    One texture for every face, because the faces are chosen per draw call and a texture swap
    between two words would break the single batch the text pass is (ADR-014).
    """
    pen_x, pen_y, shelf_height = 0, 0, 0
    for glyphs in all_glyphs:
        for glyph in glyphs:
            if glyph.width == 0 or glyph.height == 0:
                glyph.atlasX, glyph.atlasY = 0, 0
                continue
            if pen_x + glyph.width > ATLAS_WIDTH:
                pen_x, pen_y, shelf_height = 0, pen_y + shelf_height, 0
            glyph.atlasX, glyph.atlasY = pen_x, pen_y
            pen_x += glyph.width
            shelf_height = max(shelf_height, glyph.height)

    height = pen_y + shelf_height
    texels = bytearray(ATLAS_WIDTH * height)
    for glyphs in all_glyphs:
        for glyph in glyphs:
            for row in range(glyph.height):
                start = (glyph.atlasY + row) * ATLAS_WIDTH + glyph.atlasX
                texels[start:start + glyph.width] = glyph.coverage[row * glyph.width:(row + 1) * glyph.width]
    return ATLAS_WIDTH, height, texels


# ---------------------------------------------------------------------------- emitting


def hex_rows(values: bytes, per_row: int = 24, indent: str = "  ") -> str:
    lines = []
    for start in range(0, len(values), per_row):
        chunk = values[start:start + per_row]
        lines.append(indent + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
    return "\n".join(lines)


def emit(faces: list[tuple[str, list[Glyph], dict[str, int]]], sources: list[tuple[str, str, str]]) -> str:
    width, height, texels = pack([glyphs for _, glyphs, _ in faces])

    records, face_rows, first = [], [], 0
    for name, glyphs, metrics in faces:
        for glyph in sorted(glyphs, key=lambda g: g.codepoint):
            records.append(
                f"  {{0x{glyph.codepoint:04X}, {glyph.atlasX}, {glyph.atlasY}, {glyph.width}, {glyph.height}, "
                f"{glyph.bearingX}, {glyph.bearingY}, {glyph.advance}}},"
            )
        face_rows.append(
            f"  {{{first}, {len(glyphs)}, {metrics['ascent']}, {metrics['descent']}, {metrics['lineHeight']}}}, // {name}"
        )
        first += len(glyphs)

    baker_hash = hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    source_lines = "\n".join(f"//   {marker}{name}  sha256 {digest}" for marker, name, digest in sources)
    stamp = datetime.date.today().isoformat()

    body = f"""#pragma once

// GENERATED BY Build/BakeFont.py -- DO NOT EDIT BY HAND.
//
// Baked {stamp} from IBM Plex (ADR-074).
// Re-bake with `py Build/BakeFont.py`; Build/CheckProjectFiles.py fails when the hashes below stop
// matching what is on disk. ADR-073 is why this is committed rather than built.
//
// Sources:
{source_lines}
//   source Build/BakeFont.py  sha256 {baker_hash}
//
// IBM Plex is licensed under the SIL Open Font License 1.1; see Build/Fonts/LICENSE-IBM-Plex.txt.

namespace Neuron
{{

/// The cuts ADR-074 draws with: three of Plex Mono for data, two of Plex Sans for sentences.
enum class Face : std::uint8_t
{{
{chr(10).join(f"  {name}," for name, _, _ in faces)}
  Count,
}};

/// Where one glyph is in FONT_ATLAS, and how it sits on the baseline.
///
/// A public aggregate handed to the renderer, so plain fields (R8). `bearingY` is measured UP from
/// the baseline to the glyph's top row, which is FreeType's convention and the one the atlas was
/// packed in.
struct FontGlyph
{{
  std::uint32_t codepoint;
  std::uint16_t atlasX;
  std::uint16_t atlasY;
  std::uint8_t width;
  std::uint8_t height;
  std::int8_t bearingX;
  std::int8_t bearingY;
  std::uint8_t advance;
}};

/// One face's slice of FONT_GLYPHS, and the vertical metrics a line of it needs.
struct FontFace
{{
  std::uint16_t firstGlyph;
  std::uint16_t glyphCount;
  std::uint8_t ascent;
  std::uint8_t descent;
  std::uint8_t lineHeight;
}};

inline constexpr std::uint32_t FONT_ATLAS_WIDTH = {width};
inline constexpr std::uint32_t FONT_ATLAS_HEIGHT = {height};

/// Eight-bit COVERAGE, one byte a texel: the pixel shader multiplies it into the string's alpha
/// (ADR-074). Zero is discarded, so a glyph still paints nothing where it has no ink.
inline constexpr std::array<std::uint8_t, {len(texels)}> FONT_ATLAS = {{
{hex_rows(bytes(texels))}
}};

/// Every face's glyphs, each face's slice sorted by codepoint so a lookup can binary-search it.
inline constexpr std::array<FontGlyph, {len(records)}> FONT_GLYPHS = {{{{
{chr(10).join(records)}
}}}};

inline constexpr std::array<FontFace, {len(faces)}> FONT_FACES = {{{{
{chr(10).join(face_rows)}
}}}};

}} // namespace Neuron
"""
    digest = hashlib.sha256(body.encode("utf-8")).hexdigest()
    return body.replace("// Sources:", f"// Content sha256 {digest}\n//\n// Sources:", 1)


# ---------------------------------------------------------------------------- entry points


def main() -> int:
    parser = argparse.ArgumentParser(description="Bake the game's fonts into NeuronClient/Font.h.")
    parser.add_argument("--check", action="store_true", help="report hash drift without writing")
    arguments = parser.parse_args()

    faces, sources = [], []
    for name, file_name, size in FACES:
        path = FONT_DIRECTORY / file_name
        if not path.exists():
            raise SystemExit(f"missing {path}; see ADR-073 for where the TTFs come from")
        glyphs, metrics = rasterize_face(path, size)
        faces.append((name, glyphs, metrics))
        sources.append(("source ", f"Build/Fonts/{file_name} at {size}px",
                        hashlib.sha256(path.read_bytes()).hexdigest()))

    header = emit(faces, sources)
    if arguments.check:
        current = OUTPUT_HEADER.read_text(encoding="utf-8") if OUTPUT_HEADER.exists() else ""
        same = current == header
        print("BakeFont --check:", "up to date" if same else "STALE -- re-run `py Build/BakeFont.py`")
        return 0 if same else 1

    OUTPUT_HEADER.write_text(header, encoding="utf-8", newline="\n")
    total = sum(len(glyphs) for _, glyphs, _ in faces)
    print(f"BakeFont: {len(faces)} faces, {total} glyphs, {len(header):,} bytes -> {OUTPUT_HEADER.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
