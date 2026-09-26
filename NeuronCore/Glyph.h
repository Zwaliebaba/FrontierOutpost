#ifndef UI_Glyph_h__
#define UI_Glyph_h__

#include "UiCommon.h"
#include "AutoClass.h"
#include "BaseType.h"
#include "Color.h"
#include "Reference.h"
#include "V3.h"

AutoClass(GlyphState,
  V2, center,
  V2, scale,
  Color, color,
  float, alpha)

  GlyphState() :
    center(0),
    scale(1),
    color(1),
    alpha(1)
    {}
};

struct GlyphT : public RefCounted {
  BASE_TYPE(GlyphT)

  virtual Glyph Clone() const = 0;

  LT_API void Draw(GlyphState const& state = GlyphState()) const;

  /* The pixel shader the widget renderer draws the glyph with, by its legacy
     name; the vertex shader is always widget.jsl. */
  virtual char const* GetShaderName() const = 0;

  virtual Type GetVertexFormat() const = 0;

  virtual void Submit(void* vertices, GlyphState const& state) const = 0;

  virtual Glyph Transform(V2 const& offset, V2 const& scale) = 0;

  FIELDS {}
};

/* How glyphs are drawn: the renderer installs it. Without one, as on a server,
   a glyph draws nothing (ADR-014). */
LT_API void Glyph_SetRenderer(void (*draw)(GlyphT const*, GlyphState const&));

#endif
