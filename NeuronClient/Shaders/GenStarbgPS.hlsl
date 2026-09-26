// FrontierOutpost/src/liblt/Shaders/GenStarbgPS.hlsl
//
// GameData/shader/fragment/gen/starbg.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"

void Shade() {
  float r = length(2.0 * uv - 1.0);
  float a =
    3.00 * exp(-pow2(32.0 * r)) +
    0.50 * exp(-9.0 * sqrt(r));
  RETURN(((float4)(a)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
