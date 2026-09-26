// FrontierOutpost/src/liblt/Shaders/RailPS.hlsl
//
// GameData/shader/fragment/rail.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"

static float3 position = 0.0;
static float3 normal = 0.0;

float3 baseColor;
float opacity;

void Shade() {
  float2 uvp = abs(uv);
  float alpha = 5.0 * exp(-sqrt(uvp.x * 32.0)) + 0.5 * exp(-uvp.x * 4.0);
  alpha *= 0.25 * opacity * (1.0 - exp(-(1.0 - abs(2.0 * uv.y - 1.0)) * 4.0));
  alpha *= 1.0 - exp(-(1.0 - uvp.x) * 8.0);
  float3 color = baseColor;
  RETURN(float4(color, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  position = input.position;
  normal = input.normal;
  Shade();
  return fragment_color0;
}
