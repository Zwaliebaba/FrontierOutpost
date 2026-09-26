// FrontierOutpost/src/liblt/Shaders/PostGlowthreshPS.hlsl
//
// GameData/shader/fragment/post/glowthresh.jsl, in HLSL.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Math.hlsli"

TEXTURE2D(texture_);
float2 rcpFrame;

void Shade() {
  float3 c0 = (texture2D(texture_, uv + float2(0, 0) * rcpFrame).xyz);
  float3 c1 = (texture2D(texture_, uv + float2(1, 0) * rcpFrame).xyz);
  float3 c2 = (texture2D(texture_, uv + float2(0, 1) * rcpFrame).xyz);
  float3 c3 = (texture2D(texture_, uv + float2(1, 1) * rcpFrame).xyz);
  float3 c = 0.25 * (c0 + c1 + c2 + c3);
  RETURN(float4(c, 0.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
