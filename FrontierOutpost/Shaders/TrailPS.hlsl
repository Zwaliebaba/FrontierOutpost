// FrontierOutpost/src/liblt/Shaders/TrailPS.hlsl
//
// GameData/shader/fragment/trail.jsl, in HLSL.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

static float alpha = 0.0;
static float3 position = 0.0;

float3 color;
float maxAlpha;
float t;
float totalLength;

void Shade() {
  float u = uv.x;
  float v = saturate(uv.y);
  float t = max(0.0, max(0.0, abs(u) - 0.0) - 0.25 * sqrt(v));
  float a = exp(-8.0 * t) + 0.00 * exp(-8.0 * sqrt(t));
  a *= 1.0 * (1.0 - v);
  a *= 1.0 - getFoginess(length(position - eye));
  float3 c = color;
  RETURN(float4(1.0, 1.0, 1.0, 1.0) * a);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  alpha = input.alpha;
  position = input.position;
  Shade();
  return fragment_color0;
}
