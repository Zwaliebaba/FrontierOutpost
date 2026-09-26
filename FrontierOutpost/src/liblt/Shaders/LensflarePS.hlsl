// FrontierOutpost/src/liblt/Shaders/LensflarePS.hlsl
//
// GameData/shader/fragment/lensflare.jsl, in HLSL. Its position varying, which the .jsl declared as
// one float where the vertex shaders write three, and never read, is left out.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Lighting.hlsli"

float3 baseColor;
float depth;
float opacity;
TEXTURE2D(texture_);

void Shade() {
  float alpha = 1.25 * texture2D(texture_, 0.5 + 0.5 * uv).x;
  alpha *= opacity;
  alpha *= sqrt(1.0 - getFoginess(depth));
  float3 color = baseColor;
  RETURN(float4(color, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
