// FrontierOutpost/src/liblt/Shaders/PulseHeadPS.hlsl
//
// GameData/shader/fragment/pulse_head.jsl, in HLSL.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

static float3 position = 0.0;

float3 color;
float opacity;

void Shade() {
  float r = length(uv);
  float alpha = 0.0;
  alpha += exp(-sqrt(256.0 * r));
  alpha += exp(-sqrt(128.0 * r));
  // alpha += exp(-8.0 * sqrt(abs(uv.x))) * exp(-64.0 * r);
  // alpha += exp(-8.0 * sqrt(abs(uv.y))) * exp(-16.0 * r);
  alpha *= 4.0;
  alpha *= opacity;
  alpha *= 1.0 - getFoginess(length(position - eye));
  float3 c = color;
  RETURN(float4(c, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  position = input.position;
  Shade();
  return fragment_color0;
}
