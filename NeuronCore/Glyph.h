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

  virtual Shader GetShader() const = 0;

  virtual Type GetVertexFormat() const = 0;

  virtual void Submit(void* vertices, GlyphState const& state) const = 0;

  virtual Glyph Transform(V2 const& offset, V2 const& scale) = 0;

  FIELDS {}
};

#endif
