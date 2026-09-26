// FrontierOutpost/src/liblt/Shaders/GenLensflarePS.hlsl
//
// GameData/shader/fragment/gen/lensflare.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"

void Shade() {
  float2 uvp = 2.0 * uv - 1.0;
  float radius = length(uvp);
  float alpha = 0.0;
  alpha += 1.00 * exp(-sqrt(256.0 * radius));
  alpha += 0.01 * exp(-6.0 * radius);
  alpha += 0.02 * exp(-64.0 * max(0.0, abs(uvp.y) - 0.008)) * exp(-pow2(2.5 * radius));
  RETURN(((float4)(alpha)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
