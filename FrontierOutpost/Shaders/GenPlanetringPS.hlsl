// FrontierOutpost/src/liblt/Shaders/GenPlanetringPS.hlsl
//
// GameData/shader/fragment/gen/planetring.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

float seed;

void Shade() {
  float r = uv.x;
  float d = 1.0;
  d *= exp(-16.0 * max(0.0, snoise(32.0 * r + seed * 29.0) - 0.4));
  d *= exp(-8.0 * max(0.0, snoise(64.0 * r + seed * 3123.0) - 0.5));
  d *= exp(-4.0 * snoise(256.0 * r + seed * 231.0));
  d *= exp(-2.0 * snoise(2048.0 * r + seed * 990.0));
  d = 8.0 * sqrt(d);
  RETURN(((float4)(d)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
