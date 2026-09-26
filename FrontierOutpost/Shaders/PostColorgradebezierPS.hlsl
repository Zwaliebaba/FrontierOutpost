// FrontierOutpost/src/liblt/Shaders/PostColorgradebezierPS.hlsl
//
// GameData/shader/fragment/post/colorgradebezier.jsl, in HLSL.

#include "Common.hlsli"
#include "Bezier.hlsli"
#include "Color.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

TEXTURE2D(texture_);
float3 p1;
float3 p2;
float3 p3;
float3 p4;
float3 p5;

void Shade() {
  float4 c = texture2D(texture_, uv);
  c.xyz = saturate(c.xyz);
  c.x = bezier(c.x, p1.x, p2.x, p3.x, p4.x, p5.x);
  c.y = bezier(c.y, p1.y, p2.y, p3.y, p4.y, p5.y);
  c.z = bezier(c.z, p1.z, p2.z, p3.z, p4.z, p5.z);
  RETURN(float4(saturate(c.xyz), c.w));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
