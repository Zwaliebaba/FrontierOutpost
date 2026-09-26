// FrontierOutpost/src/liblt/Shaders/PostLevelsPS.hlsl
//
// GameData/shader/fragment/post/levels.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture_);
float3 lower;
float3 upper;

void Shade() {
  float4 c = texture2D(texture_, uv);
  c.xyz = (c.xyz - lower) / (upper - lower);
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
