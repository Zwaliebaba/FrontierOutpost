// FrontierOutpost/src/liblt/Shaders/PostBlurPS.hlsl
//
// GameData/shader/fragment/post/blur.jsl, in HLSL. The loop runs radius times, so its samples take
// their derivatives from uv outside it, which gives the level GL's texture2D chose.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Math.hlsli"

TEXTURE2D(texture_);
float2 delta;
int radius;
float variance;

void Shade() {
  float3 c = texture2D(texture_, uv).xyz;
  float tw = 1.0;

  float2 uvdx = ddx(uv);
  float2 uvdy = ddy(uv);
  for (int i = 1; i <= radius; ++i) {
    float offset = float(i);
    float3 c1 = texture2DGrad(texture_, uv + offset * delta.xy, uvdx, uvdy).xyz;
    float3 c2 = texture2DGrad(texture_, uv - offset * delta.xy, uvdx, uvdy).xyz;
    float w = exp(-pow2(offset / variance));
    float w1 = w;
    float w2 = w;
    c += w1 * c1;
    c += w2 * c2;
    tw += w1 + w2;
  }

  RETURN(float4(c / tw, 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
