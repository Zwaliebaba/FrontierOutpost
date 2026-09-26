// FrontierOutpost/src/liblt/Shaders/StarbgPS.hlsl
//
// GameData/shader/fragment/starbg.jsl, in HLSL.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Lighting.hlsli"

static float3 position = 0.0;

TEXTURE2D(texture_);

void Shade() {
  float r = length(uv);
  float a =
    0.50 * exp(-pow2(32.0 * r)) +
    0.20 * exp(-sqrt(81.0 * r));

  float fog = getFoginess(farPlane);
  float3 c = vertnormal;
  float4 bg = textureCube(envMap, vertpos);
  c *= lerp(((float3)(1.0)), bg.xyz, saturate(0.5 * bg.w));
  RETURN(float4(c * a * (1.0 - 0.75 * fog), 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  position = input.position;
  Shade();
  return fragment_color0;
}
