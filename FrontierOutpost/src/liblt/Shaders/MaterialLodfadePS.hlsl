// FrontierOutpost/src/liblt/Shaders/MaterialLodfadePS.hlsl
//
// GameData/shader/fragment/material/lodfade.jsl, in HLSL.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"
#include "Texturing.hlsli"

static float3 normal = 0.0;
static float3 position = 0.0;

TEXTURE2D(depthBuffer);

void Shade() {
  EARLY_Z

  float d = length(position - eye);
  float3 V = normalize(position - eye);

  float3 c = toLinear(textureCube(envMap, normal).xyz);
  float3 N = normalize(normal);
  float alpha = 1.0 - exp(-max(0.0, d / 10000.0 - 2.0));
  alpha *= exp(-4.0 * abs(c.r - 0.1));
  alpha *= 1.0 - getFoginess(d);
  alpha *= 0.0;
  RETURN(float4(c, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  normal = input.normal;
  position = input.position;
  Shade();
  return fragment_color0;
}
