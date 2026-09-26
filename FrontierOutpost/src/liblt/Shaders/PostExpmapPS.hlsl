// FrontierOutpost/src/liblt/Shaders/PostExpmapPS.hlsl
//
// GameData/shader/fragment/post/expmap.jsl, in HLSL.

#include "Common.hlsli"

TEXTURE2D(texture_);
float power;
float mult;

void Shade() {
  float4 c = texture2D(texture_, uv);
  c.xyz = (1.0 - exp(-mult * pow(abs(c.xyz), ((float3)(power)))));
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
