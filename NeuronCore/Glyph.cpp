#include "Glyph.h"

namespace {
  /* The renderer's, once it has installed it (Glyph_SetRenderer). */
  void (*drawGlyph)(GlyphT const*, GlyphState const&) = nullptr;
}

void Glyph_SetRenderer(void (*draw)(GlyphT const*, GlyphState const&)) {
  drawGlyph = draw;
}

void GlyphT::Draw(GlyphState const& state) const {
  if (drawGlyph)
    drawGlyph(this, state);
}
