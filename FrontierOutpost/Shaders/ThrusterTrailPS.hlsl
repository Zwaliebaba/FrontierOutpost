// FrontierOutpost/src/liblt/Shaders/ThrusterTrailPS.hlsl
//
// GameData/shader/fragment/thruster_trail.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Lighting.hlsli"
#include "Noise.hlsli"

static float3 position = 0.0;
static float3 normal = 0.0;

float t;
float speed;
float thinness;
float3 baseColor;

void Shade() {
  float u = uv.x;
  float v = saturate(uv.y);
  float uw = max(0.0, abs(u) - 0.25 * sqrt(v));
  float alpha = exp(-8.0 * uw) + 0.25 * exp(-8.0 * sqrt(uw));
  alpha *= (1.0 - exp(-pow2(16.0 * v))) * exp(-7.0 * v);
  alpha *= 1.0 + 4.0 * exp(-12.0 * v);
  float variation = pow2(fsnoise(float2(uv.x, 20.0 * v - 10.0 * t), 2, 1.6));
  alpha *= lerp(1.0, 1.5, variation);
  alpha *= 1.0 - getFoginess(length(position - eye));
  alpha *= 2.0;
  RETURN(float4(baseColor, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  position = input.position;
  normal = input.normal;
  Shade();
  return fragment_color0;
}
