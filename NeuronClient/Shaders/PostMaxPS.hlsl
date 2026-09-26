// FrontierOutpost/src/liblt/Shaders/PostMaxPS.hlsl
//
// GameData/shader/fragment/post/max.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture1);
TEXTURE2D(texture2);

void Shade() {
  float4 c1 = texture2D(texture1, uv);
  float4 c2 = texture2D(texture2, uv);
  RETURN(max(c1, c2));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
