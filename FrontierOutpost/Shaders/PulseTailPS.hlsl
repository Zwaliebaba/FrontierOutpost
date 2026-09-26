// FrontierOutpost/src/liblt/Shaders/PulseTailPS.hlsl
//
// GameData/shader/fragment/pulse_tail.jsl, in HLSL.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"

static float3 position = 0.0;

float3 color;
float opacity;

void Shade() {
  float u = uv.x;
  float v = uv.y;
  u = max(0.0, abs(u) - 0.005);
  float alpha = 0.0;
  alpha += exp(-sqrt(256.0 * u));
  alpha += exp(-sqrt(128.0 * u));
  alpha *= 1.0 + 8.0 * exp(-pow2(16.0 * v));
  // alpha += 1.0 * (1.0 - exp(-pow2(32.0 * v))) * exp(-sqrt(48.0 * u));
  alpha *= exp(-pow2(4.0 * v));
  alpha *= 1.0 - exp(-pow2(16.0 * v));
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
