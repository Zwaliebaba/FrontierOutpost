// FrontierOutpost/src/liblt/Shaders/FilterAoPS.hlsl
//
// GameData/shader/fragment/filter_ao.jsl, in HLSL. The loop runs as samples says, so its samples take
// their derivatives from uvp outside it. GL's took them from coordinates each pixel's noise moved,
// which read a coarser mip than the filter renders at; these read the level uvp gives, so the
// occlusion is sharper.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

TEXTURE2D(texture_);
float strength;
float radius;
int samples;

void Shade() {
  float2 uvp = uv;
  uvp.y = 1. - uvp.y;
  float occlusion = 0.;
  float h = texture2D(texture_, uvp).x;
  float totalWeight = 0.0;

  float2 uvdx = ddx(uvp);
  float2 uvdy = ddy(uvp);
  for (int i = 0; i < samples; ++i) {
    float angle = radians(360.0) * float(i) / float(samples);
    float thisRadius = noise_(float3(uvp, float(i)));
    float thisWeight = 1;
    thisRadius *= radius;
    float x = saturate(thisRadius * cos(angle) + uvp.x);
    float y = saturate(thisRadius * sin(angle) + uvp.y);
    float thisHeight = texture2DGrad(texture_, float2(x, y), uvdx, uvdy).x;

    float diff = saturate(thisHeight - h);
    occlusion += thisWeight * diff;
    totalWeight += thisWeight;
  }

  occlusion /= totalWeight;
  occlusion = saturate(strength * sqrt(occlusion));
  RETURN(((float4)(h * (1.0 - occlusion))));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
