// FrontierOutpost/src/liblt/Shaders/PostEdgedetectPS.hlsl
//
// GameData/shader/fragment/post/edgedetect.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture_);
float2 rcpFrame;

void Shade() {
  float4 n11 = texture2D(texture_, uv + float2(-1.0, -1.0) * rcpFrame);
  float4 n21 = texture2D(texture_, uv + float2( 0.0, -1.0) * rcpFrame);
  float4 n31 = texture2D(texture_, uv + float2( 1.0, -1.0) * rcpFrame);
  float4 n12 = texture2D(texture_, uv + float2(-1.0,  0.0) * rcpFrame);
  float4 n22 = texture2D(texture_, uv + float2( 0.0,  0.0) * rcpFrame);
  float4 n32 = texture2D(texture_, uv + float2( 1.0,  0.0) * rcpFrame);
  float4 n13 = texture2D(texture_, uv + float2(-1.0,  1.0) * rcpFrame);
  float4 n23 = texture2D(texture_, uv + float2( 0.0,  1.0) * rcpFrame);
  float4 n33 = texture2D(texture_, uv + float2( 1.0,  1.0) * rcpFrame);

  float4 dx = 2.0 * (n32 - n12) + (n31 - n11) + (n33 - n13);
  float4 dy = 2.0 * (n23 - n21) + (n13 - n11) + (n33 - n31);

  float4 dc = float4(
    length(float2(dx.x, dy.x)),
    length(float2(dx.y, dy.y)),
    length(float2(dx.z, dy.z)),
    length(float2(dx.w, dy.w)));
  RETURN(dc);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
