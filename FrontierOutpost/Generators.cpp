#include "Generators.h"

#include "ShaderInstance.h"
#include "StackFrame.h"
#include "Texture2D.h"

namespace {
  Texture2D LoadTexture2DFn(Location const& source) {
    return Texture_LoadFrom(source);
  }

  Texture2D ShaderInstanceFn(
    size_t const& width,
    size_t const& height,
    ShaderInstance const& shader)
  {
    SFRAME("Generate Texture");
    Texture2D texture = Texture_Create(width, height);
    shader->Begin();
    Texture_Generate(texture, shader->GetShader());
    shader->End();
    return texture;
  }
}

Generic<Texture2D> Generator_LoadTexture2D(Location const& source) {
  return Cached(Bind(FreeFn(LoadTexture2DFn), source));
}

Generic<Texture2D> Generator_ShaderInstance(
  size_t width,
  size_t height,
  ShaderInstance const& shader)
{
  return Cached(Bind(FreeFn(ShaderInstanceFn), width, height, shader));
}
