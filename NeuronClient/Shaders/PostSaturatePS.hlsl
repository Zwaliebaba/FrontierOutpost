// FrontierOutpost/src/liblt/Shaders/PostSaturatePS.hlsl
//
// GameData/shader/fragment/post/saturate.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture_);
float mult;

void Shade() {
  float4 c = texture2D(texture_, uv);
  float average = (c.x + c.y + c.z) / 3.0;
  c.xyz = ((float3)(average)) + mult * (c.xyz - ((float3)(average)));
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
