// FrontierOutpost/src/liblt/Shaders/PostLensflareCompositePS.hlsl
//
// GameData/shader/fragment/post/lensflare_composite.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture1);
TEXTURE2D(texture2);
TEXTURE2D(dirtTexture);

void Shade() {
  float4 c1 = texture2D(texture1, uv);
  float4 c2 = texture2D(texture2, uv);
  float4 dirt = texture2D(dirtTexture, uv);
  RETURN(c1 + c2 + 0.5 * c2 * dirt);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
