// FrontierOutpost/src/liblt/Shaders/ComputeLensflareVisibilityPS.hlsl
//
// GameData/shader/fragment/compute/lensflare_visibility.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"

TEXTURE2D(flareBuffer);
TEXTURE2D(depthBuffer);

void Shade() {
  float4 flare = texture2D(flareBuffer, uv);
  float depth = flare.z;
  float delta = texture2D(depthBuffer, 0.5 * flare.xy + 0.5).x - depth;
  float visibility = exp(min(0.0, delta + depth / 100.0));
  RETURN(((float4)(visibility)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
