// FrontierOutpost/src/liblt/Shaders/PostAddPS.hlsl
//
// GameData/shader/fragment/post/add.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture1);
TEXTURE2D(texture2);
float weight1;
float weight2;

void Shade() {
  float4 c1 = texture2D(texture1, uv);
  float4 c2 = texture2D(texture2, uv);
  RETURN(weight1 * c1 + weight2 * c2);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
