// FrontierOutpost/src/liblt/Shaders/DustfleckPS.hlsl
//
// GameData/shader/fragment/dustfleck.jsl, in HLSL.

#include "Common.hlsli"
#include "Bezier.hlsli"
#include "Lighting.hlsli"
#include "Noise.hlsli"

static float3 position = 0.0;
static float opacity = 0.0;

float2 size;

void Shade() {
  float2 uvp = uv;
  float y = 0.5 * uv.y + 0.5;
  float d = 0.0;
  d += exp(-sqrt(16.0 * max(0.0, abs(uv.x) - 0.01)));
  d *= 0.1;
  float3 c = ((float3)(1.0)) * d;
  c *= 1.0 - exp(-length(position) / 100.0);
  RETURN(float4(c, 0.5 * opacity));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  position = input.position;
  opacity = input.opacity;
  Shade();
  return fragment_color0;
}
